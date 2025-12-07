/* Edge Impulse Arduino examples
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// If your target is limited in memory remove this macro to save 10K RAM
#define EIDSP_QUANTIZE_FILTERBANK   0

/*
 ** NOTE: If you run into TFLite arena allocation issue.
 **
 ** This may be due to may dynamic memory fragmentation.
 ** Try defining "-DEI_CLASSIFIER_ALLOCATION_STATIC" in boards.local.txt (create
 ** if it doesn't exist) and copy this file to
 ** `<ARDUINO_CORE_INSTALL_PATH>/arduino/hardware/<mbed_core>/<core_version>/`.
 **
 ** See
 ** (https://support.arduino.cc/hc/en-us/articles/360012076960-Where-are-the-installed-cores-located-)
 ** to find where Arduino installs cores on your machine.
 **
 ** If the problem persists then there's not enough memory for this model and application.
 */

/* Includes ---------------------------------------------------------------- */
#include <XIAO-ESP32S3-KWS_inferencing.h>

#include <I2S.h>
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16

#define LED_BUILT_IN 21 

// ---- Speech to GPT includes ----
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ChatGPT.hpp>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

/** Audio buffers, pointers and selectors */
typedef struct {
    int16_t *buffer;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static bool record_status = true;
// Flag to avoid retriggering while LED is on
static bool speechTriggered = false;

// ---- Speech to GPT globals ----
// Variables to be used in the recording program
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 2
#define RECORD_TIME 5  // seconds, The maximum value is 240

// Replace with your WiFi credentials
const char* ssid = "wifi-ssid";
const char* password = "wifi-password";

// Number of bytes required for the recording buffer
uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;

File file;
const char filename[] = "/recording.wav";
bool isWIFIConnected;

String chatgpt_Q;

TaskHandle_t chatgpt_handle;
WiFiClientSecure client;
ChatGPT<WiFiClientSecure> chat_gpt(&client, "v1", "OpenAI-TOKEN");

/**
 * @brief      Arduino setup function
 */
void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    // comment out the below line to cancel the wait for USB connection (needed for native USB)
    while (!Serial);
    Serial.println("Edge Impulse Inferencing Demo");

    pinMode(LED_BUILT_IN, OUTPUT); // Set the pin as output
    digitalWrite(LED_BUILT_IN, HIGH); //Turn off
    
    I2S.setAllPins(-1, 42, 41, -1, -1);
    if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, SAMPLE_BITS)) {
      Serial.println("Failed to initialize I2S!");
    while (1) ;
  }
    
    // summary of inferencing settings (from model_metadata.h)
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: ");
    ei_printf_float((float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf(" ms.\n");
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);

    if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
        ei_printf("ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        return;
    }

    ei_printf("Recording...\n");
        // Initialize SD and start WiFi/ChatGPT tasks for speech-to-GPT
        if(!SD.begin()){
            Serial.println("Failed to mount SD Card!");
            // continue even if SD not available
        }

        // Start WiFi connection task and ChatGPT response task
        xTaskCreate(wifiConnect, "wifi_Connect", 4096, NULL, 0, NULL);
        delay(500);
        xTaskCreate(chatgpt, "chatgpt", 1024 * 8, NULL, 2, &chatgpt_handle);
}

/**
 * @brief      Arduino main function. Runs the inferencing loop.
 */
void loop()
{
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    int pred_index = 0;     // Initialize pred_index
    float pred_value = 0;   // Initialize pred_value

    // print the predictions
    ei_printf("Predictions ");
    ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
        result.timing.dsp, result.timing.classification, result.timing.anomaly);
    ei_printf(": \n");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: ", result.classification[ix].label);
        ei_printf_float(result.classification[ix].value);
        ei_printf("\n");

        if (result.classification[ix].value > pred_value){
           pred_index = ix;
           pred_value = result.classification[ix].value;
      }
    }
        // Display inference result
        if (pred_index == 3){
            digitalWrite(LED_BUILT_IN, LOW); //Turn on
            // trigger speech-to-GPT once per LED on transition
            if(!speechTriggered){
                speechTriggered = true;
                // start a one-shot recording/upload task
                xTaskCreate(speech_record_task, "speech_rec", 1024 * 16, NULL, 1, NULL);
            }
        }
        else{
            digitalWrite(LED_BUILT_IN, HIGH); //Turn off
            speechTriggered = false;
        }

    
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    ei_printf("    anomaly score: ");
    ei_printf_float(result.anomaly);
    ei_printf("\n");
#endif

// ---------------------------------------------------------------------------
// Speech-to-GPT helper functions & tasks
// ---------------------------------------------------------------------------

void speech_record_task(void *arg){
    // one-shot recording -> save wav -> upload -> notify chatgpt
    uint32_t sample_size = 0;
    uint8_t *rec_buffer = NULL;

    Serial.printf("Ready to start one-shot recording ...\n");

    File f = SD.open(filename, FILE_WRITE);
    if(!f){
        Serial.println("Failed to open file for write");
        vTaskDelete(NULL);
    }

    uint8_t wav_header[WAV_HEADER_SIZE];
    generate_wav_header(wav_header, record_size, SAMPLE_RATE);
    f.write(wav_header, WAV_HEADER_SIZE);

    rec_buffer = (uint8_t *)ps_malloc(record_size);
    if (rec_buffer == NULL) {
        Serial.printf("malloc failed!\n");
        f.close();
        vTaskDelete(NULL);
    }

    Serial.printf("Recording buffer allocated: %u bytes\n", record_size);
    esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, rec_buffer, record_size, &sample_size, portMAX_DELAY);
    if (sample_size == 0) {
        Serial.printf("Record Failed!\n");
    } else {
        Serial.printf("Recorded %d bytes\n", sample_size);
    }

    // Increase volume
    for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
        (*(uint16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
    }

    Serial.printf("Writing to the file ...\n");
    if (f.write(rec_buffer, record_size) != record_size)
        Serial.printf("Write file Failed!\n");

    free(rec_buffer);
    f.close();
    Serial.printf("The recording is over.\n");

    listDir(SD, "/", 0);

    bool uploadStatus = false;
    if(isWIFIConnected){
        uploadStatus = uploadFile();
    } else {
        Serial.println("WiFi not connected, skipping upload");
    }

    if(uploadStatus && chatgpt_handle){
        xTaskNotifyGive(chatgpt_handle);
    }

    // done
    vTaskDelete(NULL);
}

void wifiConnect(void *pvParameters){
    isWIFIConnected = false;
    Serial.print("Try to connect to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while(WiFi.status() != WL_CONNECTED){
        vTaskDelay(500);
        Serial.print(".");
    }
    Serial.println("Wi-Fi Connected!");
    isWIFIConnected = true;
    // Ignore SSL certificate validation for ChatGPT client
    client.setInsecure();
    while(true){
        vTaskDelay(1000);
    }
}

void chatgpt(void *pvParameters){
    while(1){
        // Waiting for notification signal from recording task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        String result;
        if (chat_gpt.simple_message("gpt-3.5-turbo-0301", "user", chatgpt_Q, result)) {
            Serial.println("===OK===");
            Serial.println(result);
        } else {
            Serial.println("===ERROR===");
            Serial.println(result);
        }

    }
}

void generate_wav_header(uint8_t *wav_header, uint32_t wav_size, uint32_t sample_rate)
{
    uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
    uint32_t byte_rate = SAMPLE_RATE * SAMPLE_BITS / 8;
    const uint8_t set_wav_header[] = {
        'R', 'I', 'F', 'F',
        file_size, file_size >> 8, file_size >> 16, file_size >> 24,
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        0x10, 0x00, 0x00, 0x00,
        0x01, 0x00,
        0x01, 0x00,
        sample_rate, sample_rate >> 8, sample_rate >> 16, sample_rate >> 24,
        byte_rate, byte_rate >> 8, byte_rate >> 16, byte_rate >> 24,
        0x02, 0x00,
        0x10, 0x00,
        'd', 'a', 't', 'a',
        wav_size, wav_size >> 8, wav_size >> 16, wav_size >> 24,
    };
    memcpy(wav_header, set_wav_header, sizeof(set_wav_header));
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
        Serial.printf("Listing directory: %s\n", dirname);

        File root = fs.open(dirname);
        if(!root){
                Serial.println("Failed to open directory");
                return;
        }
        if(!root.isDirectory()){
                Serial.println("Not a directory");
                return;
        }

        File file = root.openNextFile();
        while(file){
                if(file.isDirectory()){
                        Serial.print("  DIR : ");
                        Serial.println(file.name());
                        if(levels){
                                listDir(fs, file.path(), levels -1);
                        }
                } else {
                        Serial.print("  FILE: ");
                        Serial.print(file.name());
                        Serial.print("  SIZE: ");
                        Serial.println(file.size());
                }
                file = root.openNextFile();
        }
}

bool uploadFile(){
    file = SD.open(filename, FILE_READ);
    if(!file){
        Serial.println("FILE IS NOT AVAILABLE!");
        return false;
    }

    Serial.println("===> Upload FILE to Node.js Server");

    HTTPClient http;
    http.begin("http://192.168.1.208:8888/uploadAudio");
    http.addHeader("Content-Type", "audio/wav");
    int httpResponseCode = http.sendRequest("POST", &file, file.size());
    Serial.print("httpResponseCode : ");
    Serial.println(httpResponseCode);

    if(httpResponseCode == 200){
        String response = http.getString();
        Serial.println("==================== Transcription ====================");
        Serial.println(response);
        chatgpt_Q = response;
        Serial.println("====================      End      ====================");
        file.close();
        http.end();
        return true;
    }else{
        Serial.println("Error");
        return false;
    }
  
}
}

static void audio_inference_callback(uint32_t n_bytes)
{
    for(int i = 0; i < n_bytes>>1; i++) {
        inference.buffer[inference.buf_count++] = sampleBuffer[i];

        if(inference.buf_count >= inference.n_samples) {
          inference.buf_count = 0;
          inference.buf_ready = 1;
        }
    }
}

static void capture_samples(void* arg) {

  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;

  while (record_status) {

    /* read data at once from i2s - Modified for XIAO ESP2S3 Sense and I2S.h library */
    // i2s_read((i2s_port_t)1, (void*)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);
    esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, (void*)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);

    if (bytes_read <= 0) {
      ei_printf("Error in I2S read : %d", bytes_read);
    }
    else {
        if (bytes_read < i2s_bytes_to_read) {
        ei_printf("Partial I2S read");
        }

        // scale the data (otherwise the sound is too quiet)
        for (int x = 0; x < i2s_bytes_to_read/2; x++) {
            sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
        }

        if (record_status) {
            audio_inference_callback(i2s_bytes_to_read);
        }
        else {
            break;
        }
    }
  }
  vTaskDelete(NULL);
}

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));

    if(inference.buffer == NULL) {
        return false;
    }

    inference.buf_count  = 0;
    inference.n_samples  = n_samples;
    inference.buf_ready  = 0;

//    if (i2s_init(EI_CLASSIFIER_FREQUENCY)) {
//        ei_printf("Failed to start I2S!");
//    }

    ei_sleep(100);

    record_status = true;

    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void*)sample_buffer_size, 10, NULL);

    return true;
}

/**
 * @brief      Wait on new data
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void)
{
    bool ret = true;

    while (inference.buf_ready == 0) {
        delay(10);
    }

    inference.buf_ready = 0;
    return ret;
}

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);

    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
static void microphone_inference_end(void)
{
    free(sampleBuffer);
    ei_free(inference.buffer);
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif