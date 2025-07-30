## How to use the plugin

- Clone this repository into your Unreal Engine project’s `Plugins` folder.
- Set up [llama.cpp](https://github.com/ggml-org/llama.cpp) local server:
    - [Download precompiled binaries](https://github.com/ggml-org/llama.cpp/releases/) for your system architecture
    - Download [compatible](https://github.com/ggml-org/llama.cpp?tab=readme-ov-file#text-only) chat-based models (e.g. from [Hugging Face](https://huggingface.co/))
    - Start the server with `llama-server` executable. Default port is 8080.

- Set up [whisper.cpp](https://github.com/ggml-org/whisper.cpp) local server:
    - [Download precompiled binaries](https://github.com/ggml-org/whisper.cpp/releases) for your system architecture
    - Place the downloaded files (not the `Release/` folder) in `LocalNpcAIPlugin/ThirdParty/whisper.cpp/`
    - Download [whisper models](https://huggingface.co/ggerganov/whisper.cpp/tree/main)
    - Start the server with `whisper-server` executable. Make sure to use a different port from llama (e.g. 8000)

- Set up [Kokoro](https://huggingface.co/hexgrad/Kokoro-82M) local server:
    - Install [astral-uv](https://docs.astral.sh/uv/) and [espeak-ng](https://github.com/espeak-ng/espeak-ng/releases)
    - Clone [Kokoro-FastAPI](https://github.com/remsky/Kokoro-FastAPI) repository: <br>
        <pre>  git clone https://github.com/remsky/Kokoro-FastAPI.git 
        cd Kokoro-FastAPI </pre>
    - First run (requires internet connection):
    	- Windows (must use PowerShell):
		- Create python virtual environment: `uv venv`
        	- Download presequisites and start server (default port is 8880): <br>
        		<pre>  .\start-cpu.ps1 OR
                	.\start-gpu.ps1 </pre>
        	- If running scripts is disabled on the system, temporarily enable them: <br>
                	`Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process`
	- Linux and macOS:
		- Create python virtual environment: `uv venv`
        	- Download presequisites and start server (default port is 8880): <br>
        		<pre>  .\start-cpu.sh OR
                	.\start-gpu.sh </pre>
    - Subsequent runs: 
	- Duplicate the script (`start-cpu` or `start-gpu`) and remove the following lines:
		<pre>  uv pip install -e ".[cpu]" / ".[gpu]"
                	uv run --no-sync python docker/scripts/download_model.py --output api/src/models/v1_0 </pre>
	- Run the new script to start the server