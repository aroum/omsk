import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider
from export_noise_lut import get_noise_coeffs, SAMPLE_RATE

def frequency_response(b, a, n_fft=1024):
    """Calculate frequency response of a biquad filter"""
    w = np.linspace(0, np.pi, n_fft)
    z = np.exp(-1j * w)
    
    # Biquad Transfer Function: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
    # Note: our LUT stores a1 and a2 as positive/negative as used in the difference equation:
    # y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    # So the denominator in transfer function is 1 + a1*z^-1 + a2*z^-2
    
    num = b[0] + b[1]*z + b[2]*z**2
    den = 1.0 + a[0]*z + a[1]*z**2
    
    h = num / den
    mag = 20 * np.log10(np.abs(h) + 1e-12)
    freqs = w * SAMPLE_RATE / (2 * np.pi)
    return freqs, mag

if __name__ == '__main__':
    fig, ax = plt.subplots(figsize=(10, 6))
    plt.subplots_adjust(bottom=0.25)
    
    fig.patch.set_facecolor('#1e1e1e')
    ax.set_facecolor('#1e1e1e')
    ax.xaxis.label.set_color('white')
    ax.yaxis.label.set_color('white')
    ax.tick_params(colors='white', which='both')
    for spine in ax.spines.values():
        spine.set_edgecolor('#444444')
    ax.grid(True, alpha=0.2)
    
    color_init = 30
    mode, b0, b1, b2, a1, a2 = get_noise_coeffs(color_init)
    freqs, mag = frequency_response([b0, b1, b2], [a1, a2])
    
    [line] = ax.plot(freqs, mag, lw=2, color='#00ffcc')
    
    ax.set_ylim(-60, 10)
    ax.set_xlim(20, SAMPLE_RATE/2)
    ax.set_xscale('log')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_xlabel('Frequency (Hz)')
    
    ax_color = plt.axes([0.2, 0.1, 0.6, 0.03], facecolor='#333333')
    slider_color = Slider(ax_color, 'Color', 0, 127, valinit=color_init, valstep=1, color='#00ffcc')
    slider_color.label.set_color('white')
    slider_color.valtext.set_color('white')

    def update(val):
        color = int(slider_color.val)
        mode_val, b0, b1, b2, a1, a2 = get_noise_coeffs(color)
        
        mode_str = "White (Bypass)" if mode_val == 0 else ("LPF" if mode_val == 1 else "HPF")
        ax.set_title(f'Noise Filter: color={color} | Mode: {mode_str}', color='white')
        
        freqs, mag = frequency_response([b0, b1, b2], [a1, a2])
        line.set_data(freqs, mag)
        fig.canvas.draw_idle()

    slider_color.on_changed(update)
    update(color_init)
    
    print("Noise App running. Close window to continue.")
    plt.show()
