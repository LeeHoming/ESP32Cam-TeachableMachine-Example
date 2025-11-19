// Minimal ESP32-S3 inference sketch using EloquentTinyML (v3) and OV2640 grayscale capture.
#include <Arduino.h>
#include <tflm_esp32.h>
#include <eloquent_tinyml.h>
#include "esp_camera.h"

#include "board_config.h"
#include "model_settings.h"
#include "person_detect_model_data.h"

constexpr size_t kTensorArenaSize = 136 * 1024;
Eloquent::TF::Sequential<8, kTensorArenaSize> tf;

int8_t inputBuffer[kMaxImageSize];

static bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_96X96;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.jpeg_quality = 15;

#if CONFIG_IDF_TARGET_ESP32S3
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
  }
#endif

  return true;
}

static bool captureToBuffer() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to grab frame");
    return false;
  }

  if (fb->width != kNumCols || fb->height != kNumRows || fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("Unexpected frame format");
    esp_camera_fb_return(fb);
    return false;
  }

  const size_t length = min((int)fb->len, kMaxImageSize);
  for (size_t i = 0; i < length; ++i) {
    inputBuffer[i] = static_cast<int8_t>(static_cast<int>(fb->buf[i]) - 128);
  }

  esp_camera_fb_return(fb);
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting InferenceCam...");

  if (!initCamera()) {
    Serial.println("Camera init failed, halt");
    while (true) {
      delay(1000);
    }
  }

  tf.setNumInputs(kMaxImageSize);
  tf.setNumOutputs(kCategoryCount);
  tf.resolver.AddAveragePool2D();
  tf.resolver.AddConv2D();
  tf.resolver.AddDepthwiseConv2D();
  tf.resolver.AddReshape();
  tf.resolver.AddSoftmax();
  tf.resolver.AddFullyConnected();

  while (!tf.begin(g_person_detect_model_data).isOk()) {
    Serial.println(tf.exception.toString());
    delay(1000);
  }

  Serial.println("InferenceCam ready");
}

void loop() {
  if (!captureToBuffer()) {
    delay(1000);
    return;
  }

  if (!tf.predict(inputBuffer).isOk()) {
    Serial.println(tf.exception.toString());
    delay(1000);
    return;
  }

  Serial.println("--- Inference ---");
  for (int i = 0; i < kCategoryCount; ++i) {
    Serial.printf("%s : %.4f\n", kCategoryLabels[i], tf.output(i));
  }

  delay(1000);
}
