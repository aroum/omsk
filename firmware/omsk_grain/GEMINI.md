# Gemini Agent & Developer Guidelines (GEMINI.md)

This file contains rules and guidelines for AI coding assistants (like Gemini, Claude, etc.) and human developers working on this project.

## 🛠 Building and Testing

* **Build the project locally.** The project uses Pico C/C++ SDK.
* All builds should be done using the provided build script: `./build_all.sh`
* The target platform is **rp2350** (e.g., `./build_all.sh -p rp2350 -cs`).
* Do not attempt to use other build systems or IDE-specific build tasks.

## 🌐 Languages & Localization

* **Source Code Comments:** All code comments, documentation, and commit messages MUST be written in **English**.
* **AI Responses (in chat):** AI assistants should communicate with the user in **Russian** (Ответы в чате давать на русском языке).

## 📝 Code Comments Quality

* Do not write comments referring to the AI conversation or assistant ("as discussed in chat", "requested by user", etc.).
* Only write comments that carry independent value for future developers.
* Comments must explain *why* something is done, especially for workarounds or complex configurations.
