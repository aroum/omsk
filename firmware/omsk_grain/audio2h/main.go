package main

import (
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"sort"
	"strings"

	log "github.com/schollz/logger"
	"github.com/youpy/go-wav"
)

/*
#cgo CFLAGS: -I../src
#include "sw_config.h"
*/
import "C"

type Config struct {
	BitDepth           int     `json:"bit_depth"`
	SampleRate         int     `json:"sample_rate"`
	SilenceThresholdDb float64 `json:"silence_threshold_db"`
	EnvelopePoints     int     `json:"envelope_points"`
}

type File struct {
	Pathname  string
	Converted string
	Order     int
}

var config Config
var flagFolder string
var flagFolderOut string

func init() {
	flag.StringVar(&flagFolder, "folder-in", "flacs", "folder for finding audio")
	flag.StringVar(&flagFolderOut, "folder-out", "converted", "folder for placing converted files")
}

func main() {
	flag.Parse()
	log.SetLevel("trace")

	// Load configuration from compile-time constants defined in src/sw_config.h
	config.BitDepth = int(C.CONFIG_BIT_DEPTH)
	config.SampleRate = int(C.CONFIG_SAMPLE_RATE)
	config.SilenceThresholdDb = float64(C.CONFIG_SILENCE_THRESHOLD_DB)
	config.EnvelopePoints = int(C.CONFIG_ENVELOPE_POINTS)

	files, err := getFiles(flagFolder)
	if err != nil {
		panic(err)
	}

	err = convertFiles(files)
	if err != nil {
		panic(err)
	}

	err = audio2h(files)
	if err != nil {
		panic(err)
	}
}

func getFiles(folderName string) (files []File, err error) {
	fnames := []string{}
	err = filepath.Walk(folderName, func(pathname string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		ext := strings.ToLower(filepath.Ext(pathname))
		if ext == ".flac" || ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".ogg" {
			fnames = append(fnames, pathname)
		}
		return nil
	})
	if err != nil {
		return
	}

	files = make([]File, len(fnames))
	for i, fname := range fnames {
		files[i].Pathname = fname
		files[i].Converted = path.Join(flagFolderOut, filepath.Base(fname)+".wav")
		files[i].Order = i
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].Pathname < files[j].Pathname
	})
	return
}

func convertFiles(files []File) (err error) {
	os.RemoveAll(flagFolderOut)
	os.MkdirAll(flagFolderOut, os.ModePerm)
	for _, f := range files {
		// sox input output channels 1 rate X bits Y norm -0.1 silence 1 0.1 0.1% reverse silence 1 0.1 0.1% reverse
		cmdArgs := []string{
			f.Pathname, "-c", "1", "-r", fmt.Sprint(config.SampleRate), "-b", fmt.Sprint(config.BitDepth), f.Converted,
			"norm", "-0.1",
			"silence", "1", "0.1", fmt.Sprintf("%vd", config.SilenceThresholdDb),
			"reverse",
			"silence", "1", "0.1", fmt.Sprintf("%vd", config.SilenceThresholdDb),
			"reverse",
		}
		log.Tracef("sox %s", strings.Join(cmdArgs, " "))
		cmd := exec.Command("sox", cmdArgs...)
		stdoutStderr, err := cmd.CombinedOutput()
		if err != nil {
			log.Errorf("cmd failed: \n%s", stdoutStderr)
		}
	}
	return nil
}

func audio2h(files []File) (err error) {
	var sb strings.Builder
	var sbd strings.Builder

	sb.WriteString("#pragma once\n#include <stdint.h>\n\n")
	sb.WriteString(fmt.Sprintf("#define SAMPLE_RATE %d\n", config.SampleRate))
	sb.WriteString(fmt.Sprintf("#define NUM_SAMPLES %d\n", len(files)))
	sb.WriteString(fmt.Sprintf("#define ENV_POINTS %d\n", config.EnvelopePoints))
	
	dataType := "uint8_t"
	if config.BitDepth > 8 {
		dataType = "int16_t"
	}
	
	sampleStart := 0
	
	sb.WriteString(fmt.Sprintf("static inline %s raw_val(int s, uint32_t i);\n", dataType))
	sb.WriteString("static inline uint32_t raw_len(int s);\n\n")

	if config.BitDepth <= 8 {
		sb.WriteString("#define SAMPLE_TO_FLOAT(v) ((float)(v) / 127.5f - 1.0f)\n")
	} else {
		sb.WriteString("#define SAMPLE_TO_FLOAT(v) ((float)(v) / 32768.0f)\n")
	}
	sb.WriteString("\n")

	sbd.WriteString(fmt.Sprintf("const %s raw_audio[] = {\n", dataType))

	// Write dummy element to avoid empty array if 0 files
	if len(files) == 0 {
		sbd.WriteString("0};\n")
		sb.WriteString(sbd.String())
		// Provide stub bodies so linker is satisfied even with no samples
		sb.WriteString(fmt.Sprintf("static inline %s raw_val(int s, uint32_t idx) { (void)s; (void)idx; return 128; }\n", dataType))
		sb.WriteString("static inline uint32_t raw_len(int s) { (void)s; return 0; }\n")
		f, _ := os.Create("../src/tables/audio_data.h")
		f.WriteString(sb.String())
		f.Close()
		return nil
	}

	for i, f := range files {
		ints, envPos, envNeg, err := convertWavToEnvelopes(f.Converted)
		if err != nil {
			log.Error(err)
			continue
		}
		log.Tracef("[%3d] %s: %d samples", f.Order, f.Converted, len(ints))
		
		sb.WriteString(fmt.Sprintf("\n// filename: %s\n", filepath.Base(f.Pathname)))
		sb.WriteString(fmt.Sprintf("#define RAW_%d_SAMPLES %d\n", i, len(ints)))
		sb.WriteString(fmt.Sprintf("#define RAW_%d_START %d\n", i, sampleStart))
		
		// Write envelope data (Positive)
		sb.WriteString(fmt.Sprintf("const uint8_t env_pos_%d[%d] = {", i, config.EnvelopePoints))
		for j, v := range envPos {
			if j > 0 { sb.WriteString(", ") }
			sb.WriteString(fmt.Sprint(v))
		}
		sb.WriteString("};\n")

		// Write envelope data (Negative)
		sb.WriteString(fmt.Sprintf("const uint8_t env_neg_%d[%d] = {", i, config.EnvelopePoints))
		for j, v := range envNeg {
			if j > 0 { sb.WriteString(", ") }
			sb.WriteString(fmt.Sprint(v))
		}
		sb.WriteString("};\n")

		sampleStart += len(ints)
		
		if i > 0 { sbd.WriteString(",\n") }
		sbd.WriteString(printInts(ints, config.BitDepth))
	}
	sbd.WriteString("\n};\n\n")
	sb.WriteString("\n\n")
	sb.WriteString(sbd.String())

	// Access functions (static inline - defined in header)
	sb.WriteString(fmt.Sprintf("static inline %s raw_val(int s, uint32_t idx) {\n", dataType))
	for i := range files {
		sb.WriteString(fmt.Sprintf("\tif (s==%d) return raw_audio[idx+RAW_%d_START];\n", i, i))
	}
	sb.WriteString("\treturn raw_audio[0];\n}\n\n")

	sb.WriteString("static inline uint32_t raw_len(int s) {\n")
	for i := range files {
		sb.WriteString(fmt.Sprintf("\tif (s==%d) return RAW_%d_SAMPLES;\n", i, i))
	}
	sb.WriteString("\treturn RAW_0_SAMPLES;\n}\n\n")

	sb.WriteString("static inline const uint8_t* _env_pos_ptr_for(int s) {\n")
	for i := range files {
		sb.WriteString(fmt.Sprintf("\tif (s==%d) return env_pos_%d;\n", i, i))
	}
	sb.WriteString("\treturn 0;\n}\n\n")

	sb.WriteString("static inline const uint8_t* _env_neg_ptr_for(int s) {\n")
	for i := range files {
		sb.WriteString(fmt.Sprintf("\tif (s==%d) return env_neg_%d;\n", i, i))
	}
	sb.WriteString("\treturn 0;\n}\n\n")

	os.MkdirAll("../src/tables", os.ModePerm)
	f, err := os.Create("../src/tables/audio_data.h")
	if err != nil {
		return err
	}
	f.WriteString(sb.String())
	f.Close()
	return nil
}

func printInts(ints []int, bitDepth int) string {
	var sb strings.Builder
	sb.WriteString("\t")
	for i, v := range ints {
		if bitDepth <= 8 {
			sb.WriteString(fmt.Sprintf("0x%02x", uint8(v)))
		} else {
			sb.WriteString(fmt.Sprintf("%d", int16(v)))
		}
		if i < len(ints)-1 {
			sb.WriteString(", ")
		}
		if i > 0 && i%20 == 0 {
			sb.WriteString("\n\t")
		}
	}
	return sb.String()
}

func convertWavToEnvelopes(fname string) (vals []int, env_pos []uint8, env_neg []uint8, err error) {
	file, err := os.Open(fname)
	if err != nil {
		return nil, nil, nil, err
	}
	defer file.Close()

	reader := wav.NewReader(file)
	vals = make([]int, 0)

	for {
		samples, err := reader.ReadSamples()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, nil, nil, err
		}

		for _, sample := range samples {
			v := reader.IntValue(sample, 0)
			vals = append(vals, v)
		}
	}

	maxDeviation := 1.0
	for _, v := range vals {
		floatV := float64(v)
		if config.BitDepth <= 8 {
			floatV -= 128.0
		}
		absV := math.Abs(floatV)
		if absV > maxDeviation {
			maxDeviation = absV
		}
	}

	// Compute envelopes
	env_pos = make([]uint8, config.EnvelopePoints)
	env_neg = make([]uint8, config.EnvelopePoints)
	
	chunkSize := len(vals) / config.EnvelopePoints
	if chunkSize == 0 {
		chunkSize = 1
	}

	for i := 0; i < config.EnvelopePoints; i++ {
		start := i * chunkSize
		end := start + chunkSize
		if end > len(vals) {
			end = len(vals)
		}
		
		max_p := 0.0
		min_p := 0.0
		for j := start; j < end; j++ {
			v := float64(vals[j])
			// If 8-bit, center is 128
			if config.BitDepth <= 8 {
				v = v - 128.0
			}
			if v > max_p {
				max_p = v
			}
			if v < min_p {
				min_p = v
			}
		}
		
		normMax := maxDeviation
		
		ep := 128 + int((max_p / normMax) * 127)
		en := 128 + int((min_p / normMax) * 127)
		
		if ep > 255 { ep = 255 }
		if ep < 0 { ep = 0 }
		if en > 255 { en = 255 }
		if en < 0 { en = 0 }
		
		env_pos[i] = uint8(ep)
		env_neg[i] = uint8(en)
	}

	return vals, env_pos, env_neg, nil
}
