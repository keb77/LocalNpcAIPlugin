## Plugin Setup Instructions

- Clone this repository into your Unreal Engine project’s `Plugins` folder.
- Set up [llama.cpp](https://github.com/ggml-org/llama.cpp):
   - [Download precompiled binaries](https://github.com/ggml-org/llama.cpp/releases/) for your system architecture
   - Place the downloaded files in `LocalNpcAIPlugin/ThirdParty/llama.cpp/`
   - Download compatible **quantized GGUF** chat/instruct models (e.g. from [Hugging Face](https://huggingface.co/))
   - Place model files in `LocalNpcAIPlugin/ThirdParty/models/`
- Set up [whisper.cpp](https://github.com/ggml-org/whisper.cpp):
   - [Download precompiled binaries](https://github.com/ggml-org/whisper.cpp/releases) for your system architecture
   - Place the downloaded files (not the `Release/` folder) in `LocalNpcAIPlugin/ThirdParty/whisper.cpp/`
   - Download [whisper models](https://huggingface.co/ggerganov/whisper.cpp/tree/main)
   - Place model files in `LocalNpcAIPlugin/ThirdParty/models/`
- Set up [piper](https://github.com/rhasspy/piper):
   - [Download precompiled binaries](https://github.com/rhasspy/piper/releases) for your system architecture
   - Place the downloaded files in `LocalNpcAIPlugin/ThirdParty/piper/`. Make sure `run_piper.exe` is also there
   - Download [voice models](https://github.com/rhasspy/piper/blob/master/VOICES.md)
   - Place model files in `LocalNpcAIPlugin/ThirdParty/models/`
