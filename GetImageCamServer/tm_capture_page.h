#include <pgmspace.h>

const char tm_capture_index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <link rel="icon" href="data:," />
    <title>ESP32 TM Capture</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
    <style>
      body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 16px;
        background: #111;
        color: #f5f5f5;
      }
      main {
        max-width: 640px;
        margin: 0 auto;
      }
      img {
        width: 100%;
        max-width: 480px;
        display: block;
        margin-bottom: 12px;
        border: 1px solid #333;
      }
      label,
      button,
      .status {
        display: block;
        margin-bottom: 12px;
      }
      input,
      button {
        padding: 8px;
        font-size: 1rem;
        width: 100%;
        box-sizing: border-box;
      }
      button {
        cursor: pointer;
      }
      button:disabled {
        cursor: not-allowed;
        opacity: 0.6;
      }
      .row {
        display: flex;
        gap: 8px;
      }
      .row > * {
        flex: 1;
      }
    </style>
  </head>
  <body>
    <main>
      <h1>ESP32 Teachable Machine Capture</h1>
      <img id="preview" alt="Camera preview" />
      <label>
        Capture interval (ms)
        <input id="intervalInput" type="number" value="1000" min="200" step="100" />
      </label>
      <div class="row">
        <button id="startBtn">Start Recording</button>
        <button id="stopBtn" disabled>Stop &amp; Download</button>
      </div>
      <p class="status">
        Status: <span id="statusLabel">Idle</span> |
        Captured: <span id="captureCount">0</span>
      </p>
    </main>
    <script>
      (() => {
        const previewImg = document.getElementById("preview");
        const startBtn = document.getElementById("startBtn");
        const stopBtn = document.getElementById("stopBtn");
        const intervalInput = document.getElementById("intervalInput");
        const statusLabel = document.getElementById("statusLabel");
        const captureCount = document.getElementById("captureCount");

        const captures = [];
        const canvas = document.createElement("canvas");
        canvas.width = 96;
        canvas.height = 96;
        const ctx = canvas.getContext("2d");

        let previewUrl = "";
        let previewTimer = null;
        let recordingTimer = null;
        let isRecording = false;
        let pendingFrame = false;

        const fetchFrame = async () => {
          const response = await fetch(`/capture?_ts=${Date.now()}`);
          if (!response.ok) {
            throw new Error("Failed to fetch frame");
          }
          return response.blob();
        };

        const blobToImage = (blob) =>
          new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => {
              URL.revokeObjectURL(img.src);
              resolve(img);
            };
            img.onerror = reject;
            img.src = URL.createObjectURL(blob);
          });

        const applyGrayscale = () => {
          const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
          const { data } = imageData;
          for (let i = 0; i < data.length; i += 4) {
            const r = data[i];
            const g = data[i + 1];
            const b = data[i + 2];
            const gray = Math.round(0.299 * r + 0.587 * g + 0.114 * b);
            data[i] = gray;
            data[i + 1] = gray;
            data[i + 2] = gray;
          }
          ctx.putImageData(imageData, 0, 0);
        };

        const resizeTo96 = async (blob) => {
          ctx.clearRect(0, 0, canvas.width, canvas.height);
          try {
            const bitmap = await createImageBitmap(blob);
            ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
            bitmap.close();
          } catch (error) {
            const img = await blobToImage(blob);
            ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
          }
          applyGrayscale();
          return new Promise((resolve, reject) => {
            canvas.toBlob(
              (resized) => {
                if (resized) {
                  resolve(resized);
                } else {
                  reject(new Error("Failed to build resized blob"));
                }
              },
              "image/jpeg",
              0.92
            );
          });
        };

        const updatePreview = (blob) => {
          if (previewUrl) {
            URL.revokeObjectURL(previewUrl);
          }
          previewUrl = URL.createObjectURL(blob);
          previewImg.src = previewUrl;
        };

        const updateCount = () => {
          captureCount.textContent = captures.length.toString();
        };

        const setStatus = (text) => {
          statusLabel.textContent = text;
        };

        const takeFrame = async (save) => {
          if (pendingFrame) {
            return;
          }
          pendingFrame = true;
          try {
            const blob = await fetchFrame();
            updatePreview(blob);
            if (save) {
              const resized = await resizeTo96(blob);
              captures.push(resized);
              updateCount();
            }
          } catch (error) {
            console.error(error);
            setStatus("Camera error");
          } finally {
            pendingFrame = false;
          }
        };

        const startRecording = () => {
          if (isRecording) {
            return;
          }
          captures.length = 0;
          updateCount();
          const interval = Math.max(200, Number(intervalInput.value) || 1000);
          intervalInput.value = interval.toString();
          isRecording = true;
          setStatus("Recording...");
          startBtn.disabled = true;
          stopBtn.disabled = false;
          takeFrame(true);
          recordingTimer = setInterval(() => takeFrame(true), interval);
        };

        const buildZip = async () => {
          if (typeof window.JSZip === "undefined") {
            throw new Error("JSZip not loaded");
          }
          if (!captures.length) {
            throw new Error("Nothing to download");
          }
          const zip = new window.JSZip();
          captures.forEach((blob, index) => {
            const filename = `capture_${String(index + 1).padStart(4, "0")}.jpg`;
            zip.file(filename, blob);
          });
          return zip.generateAsync({ type: "blob" });
        };

        const stopRecording = async () => {
          if (!isRecording) {
            return;
          }
          isRecording = false;
          startBtn.disabled = false;
          stopBtn.disabled = true;
          if (recordingTimer) {
            clearInterval(recordingTimer);
            recordingTimer = null;
          }
          if (captures.length === 0) {
            setStatus("Idle");
            return;
          }
        setStatus("Preparing ZIP...");
        try {
          const zipBlob = await buildZip();
            const url = URL.createObjectURL(zipBlob);
            const link = document.createElement("a");
            link.href = url;
            link.download = `tm_captures_${Date.now()}.zip`;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            setTimeout(() => URL.revokeObjectURL(url), 1000);
            setStatus("Download ready");
        } catch (error) {
          console.error(error);
          setStatus(error && error.message ? error.message : "ZIP failed");
        } finally {
            captures.length = 0;
            updateCount();
            setTimeout(() => setStatus("Idle"), 1200);
          }
        };

        const startPreviewLoop = () => {
          previewTimer = setInterval(() => {
            if (!isRecording) {
              takeFrame(false);
            }
          }, 1300);
          takeFrame(false);
        };

        const stopPreviewLoop = () => {
          if (previewTimer) {
            clearInterval(previewTimer);
            previewTimer = null;
          }
        };

        startBtn.addEventListener("click", startRecording);
        stopBtn.addEventListener("click", stopRecording);

        window.addEventListener("beforeunload", () => {
          stopPreviewLoop();
          if (recordingTimer) {
            clearInterval(recordingTimer);
          }
        });

        startPreviewLoop();
      })();
    </script>
  </body>
</html>
)rawliteral";
