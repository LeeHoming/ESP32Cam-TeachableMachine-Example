# ESP32 Teachable Machine Toolkit

This repository hosts two Arduino sketches that cover the full workflow for Teachable Machine (TM) vision projects on ESP32S3 + OV2640 hardware:

| Sketch | Purpose |
| --- | --- |
| `GetImageCamServer` | Wi‑Fi camera server with a browser UI to preview the live feed, periodically capture 96×96 frames, and download them as a ZIP file for TM training. |
| `InferenceCam` | Runs a TM/TensorFlow Lite Micro model on the device using the same 96×96 grayscale preprocessing pipeline. |
| `Example-LED Indicator` | Same as `InferenceCam` but adds a WS2812B indicator on GPIO48 to show the top result (red/blue). |

Both sketches share the same `board_config.h`/`camera_pins.h`, so you can switch boards (ESP32S3‑EYE, ESP‑EYE, AI Thinker, etc.) by toggling the corresponding `#define` in a single place.

---

## Repository Layout

```
GetImageCamServer/      # Camera HTTP server + web UI for dataset capture
InferenceCam/           # On-device inference sketch
Example-LED Indicator/  # Inference + WS2812B indicator (ESP32S3 GPIO48)
```

Each folder is a standalone Arduino project; open them separately inside the IDE.

---

## Prerequisites

Install the following in the Arduino IDE:

1. **ESP32 Boards Package** – via Boards Manager, install “esp32 by Espressif Systems” (v2.0.11 or newer). Select an ESP32S3 target before compiling.
2. **TensorFlow Lite runtime (inference sketch only)**  
   - Library Manager → install **`tflm_esp32`**  
   - Library Manager → install **`EloquentTinyML`** (≥3.0.1). This exposes `Eloquent::TF::Sequential`, which the inference sketch uses.

`GetImageCamServer` only depends on the core ESP32 libraries that ship with the boards package (`esp_camera`, `WiFi`, `esp_http_server`, etc.).

---

## Workflow Overview

1. **Data collection** – Flash `GetImageCamServer`, open the built-in web UI, and record a dataset. All captured frames are automatically resized to **96×96** and converted to grayscale before download, matching TM’s expected input.
2. **Model training** – Upload the ZIP to [Teachable Machine](https://teachablemachine.withgoogle.com/) (Image Project → Standard image model) and export an **int8 quantized** TensorFlow Lite model.
3. **Deployment** – Convert the `.tflite` file into C arrays and replace `InferenceCam/person_detect_model_data.cpp/.h` (or the same files inside `Example-LED Indicator/`), then flash to run the model on-device.

---

## GetImageCamServer

1. **Configure Wi‑Fi & board**
   - Edit `GetImageCamServer/GetImageCamServer.ino` and set `ssid`/`password`.
   - Open `GetImageCamServer/board_config.h` and enable the line that matches your hardware (uncomment `#define CAMERA_MODEL_...` and comment out the others).

2. **Flash & connect**
   - Compile/upload the sketch.  
   - Check the serial monitor for `Camera Ready! Use 'http://<ip>' to connect`.

3. **Web interface**
   - Browse to the IP printed on serial. The page provides:
     - A live preview (`<img>` refreshed from `/capture` every ~1.3 s when idle).
     - “Start Recording” button: repeatedly fetches `/capture`, resizes each frame to 96×96 via Canvas, converts it to grayscale, and stores it in memory.
     - “Stop & Download” button: zips all stored frames with JSZip and triggers a `.zip` download (`tm_captures_<timestamp>.zip`).
     - Adjustable capture interval (default 1000 ms, min 200 ms).

4. **Endpoints**
   - `/stream` – MJPEG stream for classic preview clients.
   - `/capture` – Single JPEG frame (full resolution, typically QVGA after initial sensor tuning).
   - `/` – The Teachable Machine capture web page defined in `tm_capture_page.h`.

Use the downloaded ZIP directly in TM’s “Upload” panel—the images are already normalized to 96×96 grayscale, so no extra preprocessing is needed.

---

## InferenceCam

This sketch runs the trained TM model locally using EloquentTinyML + tflm_esp32.

1. **Board selection** – Open `InferenceCam/board_config.h` and set the same `CAMERA_MODEL_...` macro you used for the capture sketch.

2. **Install dependencies** – Confirm that both `tflm_esp32` and `EloquentTinyML` are installed; the compiler needs them for `<tflm_esp32.h>` and `<eloquent_tinyml.h>`.

3. **Prepare the model**
   - Export the TM model as **TensorFlow Lite (float32/i16/i8) – select “Quantized int8”**.
   - Convert the `.tflite` file into a C array:
     ```bash
     xxd -i your_model.tflite > person_detect_model_data.cpp
     ```
     Replace the contents of `InferenceCam/person_detect_model_data.cpp` with this output and update `person_detect_model_data.h` if the symbol names differ (by default they’re `g_person_detect_model_data` and `g_person_detect_model_data_len`).
   - Update `InferenceCam/model_settings.h/.cpp`:
     - `kNumCols`, `kNumRows`, `kNumChannels` (keep at 96×96×1 unless you trained differently).
     - `kCategoryCount`, `kCategoryLabels[]`, and any index constants (`kPersonIndex`, `kNotAPersonIndex`, or rename them to match your classes).

4. **Build & run**
   - Upload `InferenceCam.ino`.  
   - The sketch initializes `esp_camera` at 96×96 grayscale, captures one frame per second, subtracts 128 from each pixel to match the int8 quantized model input, and feeds it to `tf.predict()`.
   - Classification scores are printed over Serial:
     ```
     --- Inference ---
     classA : 0.87
     classB : 0.13
     ```
   - Adjust the sampling rate by changing the `delay(1000)` at the end of `loop()`.

5. **Adding new ops** – If TM generates a model that uses operations beyond Conv/DepthwiseConv/AveragePool/Reshape/Softmax/FullyConnected, append the corresponding `tf.resolver.AddXxx()` calls in `setup()`.

---

## Example-LED Indicator

Identical to `InferenceCam`, but it also drives a single WS2812B LED on **ESP32S3 GPIO48** to reflect the top prediction: class 0 -> red, other -> blue.

1. **Extra dependency** – Library Manager → install **Adafruit NeoPixel**.
2. **Board selection** – Same `board_config.h` toggle as `InferenceCam`.
3. **Load your model** – Replace `person_detect_model_data.cpp` and update `model_settings.*` exactly as described below.
4. **Flash & test** – Open Serial for scores; watch the LED change color after each inference.

---

## 使用自己的模型（最简单流程）

1. 在 Teachable Machine 训练好后，点击 **Export Model → TensorFlow Lite → Quantized int8** 下载 `.tflite`。
2. 把模型转成 C 数组（示例命令，macOS/Linux 终端）：
   ```bash
   xxd -i your_model.tflite > person_detect_model_data.cpp
   ```
   直接用生成的文件替换 `InferenceCam/person_detect_model_data.cpp`（或 `Example-LED Indicator/person_detect_model_data.cpp`）。如果数组名字不是 `g_person_detect_model_data`/`g_person_detect_model_data_len`，同步更新同目录下的 `.h`。
3. 打开同目录的 `model_settings.cpp/.h`，把类别数量和标签改成你在 TM 中的类名，并调整输入尺寸/通道数（默认 96×96×1 保持不变即可）。
4. 编译并烧录。串口会打印新模型的分数，`Example-LED Indicator` 还会用 LED 显示结果。

---

## Tips & Troubleshooting

| Issue | Cause / Fix |
| --- | --- |
| `Camera init failed with error 0x...` | Wrong board selected in `board_config.h` or insufficient power. Verify the pin mapping and use a stable 5V supply. |
| Browser shows `JSZip not loaded` | Network blocked cdnjs. Refresh with internet access or host JSZip locally (edit `tm_capture_page.h`). |
| TM model outputs wrong number of classes | `model_settings.*` not updated. Ensure `kCategoryCount` and `kCategoryLabels[]` match your TM project. |
| TFLite ops missing at compile time | Add the missing op (e.g., `tf.resolver.AddMul();`) before calling `tf.begin()`. |
| Serial output saturated at ±128 | Make sure you exported an **int8** model; float models need different handling. |

---

## Next Steps

- Customize the web UI (CSS/JS) in `tm_capture_page.h` to add class labels, counters, or auto-upload.
- Extend `InferenceCam` to publish results over MQTT/HTTP after prediction.
- If you need different resolutions, adjust both sketches’ sensor setup (`GetImageCamServer.ino` and `InferenceCam.ino`) and the web canvas/TM settings accordingly.

This workflow lets you iterate quickly: capture data with the ESP32, train in TM, drop the new model into `InferenceCam`, and you’re back on hardware in minutes. Happy hacking!
