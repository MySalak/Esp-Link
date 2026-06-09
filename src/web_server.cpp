#include "web_server.h"
#include "secret.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <MD5Builder.h>
#include <rom/crc.h>
#include "stm32u5_flash.h"
#include "swd_bitbang.h"
#include "swd_dp.h"

WebServer server(80);

const char* html_index = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Firmware Flasher</title>
    <style>
        body { font-family: system-ui, sans-serif; background: #0f172a; color: #f1f5f9; display: flex; justify-content: center; padding: 20px; margin: 0; min-height: 100vh; box-sizing: border-box; }
        .container { background: #1e293b; border-radius: 12px; padding: 24px; width: 100%; max-width: 400px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5); }
        h2 { margin: 0 0 20px; text-align: center; color: #38bdf8; }
        .tabs { display: flex; margin-bottom: 16px; border-bottom: 1px solid #334155; }
        .tab-btn { flex: 1; background: none; color: #94a3b8; padding: 10px; border: none; border-bottom: 2px solid transparent; cursor: pointer; font-size: 14px; font-weight: bold; }
        .tab-btn.active { color: #38bdf8; border-bottom-color: #38bdf8; }
        input, select, button.primary { width: 100%; padding: 12px; margin: 8px 0; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: #fff; box-sizing: border-box; font-size: 16px; }
        input[type=file] { padding: 9px; }
        input:focus, select:focus { border-color: #38bdf8; outline: none; }
        button.primary { background: #38bdf8; color: #0f172a; font-weight: bold; cursor: pointer; border: none; transition: 0.2s; }
        button.primary:hover:not(:disabled) { background: #7dd3fc; }
        button.primary:disabled { background: #475569; cursor: not-allowed; opacity: 0.7; }
        #status { margin-top: 16px; text-align: center; font-size: 14px; word-wrap: break-word; color: #94a3b8; }
        .hidden { display: none; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Firmware Flasher</h2>
        <div class="tabs">
            <button class="tab-btn active" onclick="showTab('jenkins')">Jenkins</button>
            <button class="tab-btn" onclick="showTab('upload')">Upload</button>
        </div>

        <div id="tab-jenkins">
            <div id="loading">Loading manifest...</div>
            <div id="main-content" class="hidden">
                <input type="text" id="search" placeholder="Search devEUI...">
                <select id="eui-select" size="5"></select>
                <div id="selected-info" style="margin: 12px 0; font-size: 14px; color: #cbd5e1;"></div>
                <button id="flash-btn" class="primary" disabled>Flash Firmware</button>
            </div>
        </div>

        <div id="tab-upload" class="hidden">
            <input type="file" id="local-file" accept=".bin">
            <input type="text" id="local-md5" placeholder="Expected MD5 Checksum (Optional)">
            <button id="upload-btn" class="primary" disabled>Upload & Flash</button>
        </div>

        <div id="status"></div>
    </div>
    <script>
        function showTab(tab) {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('#tab-jenkins, #tab-upload').forEach(el => el.classList.add('hidden'));
            document.querySelector(`.tab-btn[onclick="showTab('${tab}')"]`).classList.add('active');
            document.getElementById(`tab-${tab}`).classList.remove('hidden');
            document.getElementById('status').textContent = '';
        }

        let manifest = [];
        const statusDiv = document.getElementById('status');
        const select = document.getElementById('eui-select');
        const flashBtn = document.getElementById('flash-btn');
        const uploadBtn = document.getElementById('upload-btn');
        const localFile = document.getElementById('local-file');
        const localMd5 = document.getElementById('local-md5');

        function updateSelect(filter = '') {
            select.innerHTML = '';
            manifest.filter(item => item.devEUI.toLowerCase().includes(filter.toLowerCase())).forEach(item => {
                const opt = document.createElement('option');
                opt.value = item.devEUI;
                opt.textContent = item.devEUI;
                select.appendChild(opt);
            });
        }

        fetch('/api/manifest').then(res => res.json()).then(data => {
            manifest = data;
            document.getElementById('loading').classList.add('hidden');
            document.getElementById('main-content').classList.remove('hidden');
            updateSelect();
        }).catch(() => {
            document.getElementById('loading').textContent = 'Failed to load manifest.';
            document.getElementById('loading').style.color = '#ef4444';
        });

        document.getElementById('search').addEventListener('input', e => updateSelect(e.target.value));

        select.addEventListener('change', () => {
            const sel = manifest.find(i => i.devEUI === select.value);
            if (sel) {
                document.getElementById('selected-info').innerHTML = `File: <b>${sel.filename}</b><br>MD5: <b>${sel.md5}</b>`;
                flashBtn.disabled = false;
            }
        });

        flashBtn.addEventListener('click', () => {
            const sel = manifest.find(i => i.devEUI === select.value);
            if (!sel) return;
            flashBtn.disabled = true;
            statusDiv.textContent = 'Flashing... Please wait.';
            statusDiv.style.color = '#38bdf8';
            fetch(`/api/flash?filename=${encodeURIComponent(sel.filename)}&md5=${encodeURIComponent(sel.md5)}`, { method: 'POST' })
                .then(res => res.text().then(text => ({status: res.status, text})))
                .then(({status, text}) => {
                    statusDiv.textContent = status === 200 ? 'Flash Successful!' : 'Error: ' + text;
                    statusDiv.style.color = status === 200 ? '#22c55e' : '#ef4444';
                    flashBtn.disabled = false;
                }).catch(err => {
                    statusDiv.textContent = 'Request failed: ' + err;
                    statusDiv.style.color = '#ef4444';
                    flashBtn.disabled = false;
                });
        });

        localFile.addEventListener('change', () => {
            uploadBtn.disabled = !localFile.files.length;
        });

        uploadBtn.addEventListener('click', () => {
            if(!localFile.files.length) return;
            uploadBtn.disabled = true;
            statusDiv.textContent = 'Uploading & Flashing...';
            statusDiv.style.color = '#38bdf8';
            
            const formData = new FormData();
            formData.append('file', localFile.files[0]);

            const md5Query = localMd5.value ? `?md5=${encodeURIComponent(localMd5.value)}` : '';

            fetch('/api/upload' + md5Query, { method: 'POST', body: formData })
                .then(res => res.text().then(text => ({status: res.status, text})))
                .then(({status, text}) => {
                    statusDiv.textContent = text;
                    statusDiv.style.color = status === 200 ? '#22c55e' : '#ef4444';
                    uploadBtn.disabled = false;
                }).catch(err => {
                    statusDiv.textContent = 'Upload failed: ' + err;
                    statusDiv.style.color = '#ef4444';
                    uploadBtn.disabled = false;
                });
        });
    </script>
</body>
</html>
)rawliteral";

static String get_jenkins_url(const String& path) {
    return String("https://jenkins.mysalak.com/job/mysalak-firmware/lastSuccessfulBuild/artifact/programmer/bin/") + path;
}

void handle_index() {
    server.send(200, "text/html", html_index);
}

void handle_manifest() {
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, get_jenkins_url("manifest.json"));
    http.addHeader("Authorization", JENKINS_TOKEN);
    
    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            server.send(200, "application/json", payload);
        } else {
            server.send(httpCode, "text/plain", "Jenkins returned: " + String(httpCode));
        }
    } else {
        server.send(500, "text/plain", "Failed to connect to Jenkins: " + http.errorToString(httpCode));
    }
    http.end();
}

class FlashStream : public Stream {
public:
    uint32_t flash_addr;
    uint32_t next_unerased_addr;
    uint32_t written;
    uint8_t buffer[16];
    uint8_t buf_pos;
    bool error;
    String error_msg;
    MD5Builder md5;

    FlashStream() {
        flash_addr = FLASH_BASE_ADDR;
        next_unerased_addr = flash_addr;
        written = 0;
        buf_pos = 0;
        error = false;
        md5.begin();
    }

    size_t write(uint8_t c) override {
        return write(&c, 1);
    }

    size_t write(const uint8_t *buf, size_t size) override {
        if (error) return 0;
        md5.add((uint8_t*)buf, size);
        
        size_t written_this_call = 0;
        while (size > 0) {
            uint32_t to_copy = 16 - buf_pos;
            if (to_copy > size) to_copy = size;
            
            memcpy(buffer + buf_pos, buf, to_copy);
            buf_pos += to_copy;
            buf += to_copy;
            size -= to_copy;
            written_this_call += to_copy;

            if (buf_pos == 16) {
                if (!flush_buffer(16)) {
                    return written_this_call;
                }
            }
        }
        return written_this_call;
    }

    bool flush_buffer(uint8_t len) {
        if (len == 0) return true;
        
        while (flash_addr + written + len > next_unerased_addr) {
            uint8_t page, count, bank;
            stm32u5_flash_calc_pages(next_unerased_addr, 1, &page, &count, &bank);
            if (!stm32u5_flash_erase_page(bank, page)) {
                error = true;
                error_msg = "Failed to erase page at 0x" + String(next_unerased_addr, HEX);
                return false;
            }
            next_unerased_addr += FLASH_PAGE_SIZE;
        }

        if (!stm32u5_flash_write(flash_addr + written, buffer, len)) {
            error = true;
            error_msg = "Failed to write flash at offset " + String(written);
            return false;
        }
        written += len;
        buf_pos = 0;
        return true;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

void handle_flash() {
    if (!server.hasArg("filename") || !server.hasArg("md5")) {
        server.send(400, "text/plain", "Missing filename or md5");
        return;
    }

    String filename = server.arg("filename");
    String expected_md5 = server.arg("md5");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, get_jenkins_url(filename));
    http.addHeader("Authorization", JENKINS_TOKEN);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        server.send(500, "text/plain", "Failed to download firmware: " + String(httpCode));
        http.end();
        return;
    }

    // No file size check needed, HTTPClient::writeToStream handles chunked transfer encoding

    uint32_t idcode;
    if (!swd_connect(&idcode)) {
        server.send(500, "text/plain", "SWD Connection failed");
        http.end();
        return;
    }

    if (!swd_halt_cpu()) {
        server.send(500, "text/plain", "Failed to halt CPU");
        http.end();
        return;
    }

    if (!stm32u5_flash_unlock()) {
        server.send(500, "text/plain", "Failed to unlock flash");
        http.end();
        return;
    }

    FlashStream flashStream;
    int bytes_written = http.writeToStream(&flashStream);
    
    // Write any trailing bytes left in the buffer
    if (!flashStream.error && flashStream.buf_pos > 0) {
        flashStream.flush_buffer(flashStream.buf_pos);
    }

    flashStream.md5.calculate();
    String calculated_md5 = flashStream.md5.toString();

    uint32_t flash_crc = 0;
    if (!flashStream.error && flashStream.written > 0) {
        uint32_t verified = 0;
        uint32_t size = flashStream.written;
        uint8_t *flash_buf = (uint8_t *)malloc(1024);
        if (flash_buf) {
            while (verified < size) {
                uint32_t chunk_size = (size - verified > 1024) ? 1024 : (size - verified);
                if (!swd_mem_read_block(FLASH_BASE_ADDR + verified, flash_buf, chunk_size)) {
                    break;
                }
                flash_crc = crc32_le(flash_crc, flash_buf, chunk_size);
                verified += chunk_size;
            }
            free(flash_buf);
        }
    }

    stm32u5_flash_lock();
    swd_reset_and_run();
    http.end();

    if (flashStream.error) {
        server.send(500, "text/plain", flashStream.error_msg);
        return;
    }

    if (bytes_written <= 0) {
        server.send(500, "text/plain", "Download stream was empty or failed.");
        return;
    }

    if (!calculated_md5.equalsIgnoreCase(expected_md5)) {
        server.send(500, "text/plain", "MD5 mismatch! Flashed: " + calculated_md5 + " Expected: " + expected_md5);
    } else {
        char crc_str[32];
        snprintf(crc_str, sizeof(crc_str), " | Flash CRC32: 0x%08lX", flash_crc);
        server.send(200, "text/plain", "Success!" + String(crc_str));
    }
}

static String upload_result = "";
static int upload_status = 200;
static FlashStream* uploadStream = nullptr;

void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        upload_status = 200;
        upload_result = "Success";
        uint32_t idcode;
        if (!swd_connect(&idcode) || !swd_halt_cpu() || !stm32u5_flash_unlock()) {
            upload_status = 500;
            upload_result = "Hardware init failed";
            return;
        }
        uploadStream = new FlashStream();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadStream && !uploadStream->error && upload_status == 200) {
            uploadStream->write(upload.buf, upload.currentSize);
            if (uploadStream->error) {
                upload_status = 500;
                upload_result = uploadStream->error_msg;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadStream) {
            if (!uploadStream->error && uploadStream->buf_pos > 0) {
                uploadStream->flush_buffer(uploadStream->buf_pos);
                if (uploadStream->error) {
                    upload_status = 500;
                    upload_result = uploadStream->error_msg;
                }
            }
            uploadStream->md5.calculate();
            String calculated_md5 = uploadStream->md5.toString();
            
            String expected_md5 = server.arg("md5");

            uint32_t flash_crc = 0;
            if (!uploadStream->error && uploadStream->written > 0) {
                uint32_t verified = 0;
                uint32_t size = uploadStream->written;
                uint8_t *flash_buf = (uint8_t *)malloc(1024);
                if (flash_buf) {
                    while (verified < size) {
                        uint32_t chunk_size = (size - verified > 1024) ? 1024 : (size - verified);
                        if (!swd_mem_read_block(FLASH_BASE_ADDR + verified, flash_buf, chunk_size)) {
                            break;
                        }
                        flash_crc = crc32_le(flash_crc, flash_buf, chunk_size);
                        verified += chunk_size;
                    }
                    free(flash_buf);
                }
            }

            stm32u5_flash_lock();
            swd_reset_and_run();
            
            if (upload_status == 200) {
                char crc_str[32];
                snprintf(crc_str, sizeof(crc_str), " | Flash CRC32: 0x%08lX", flash_crc);
                
                if (expected_md5.length() > 0 && !calculated_md5.equalsIgnoreCase(expected_md5)) {
                    upload_status = 500;
                    upload_result = "MD5 mismatch! Flashed: " + calculated_md5 + " Expected: " + expected_md5;
                } else {
                    upload_result = "Success! MD5: " + calculated_md5 + String(crc_str);
                }
            }
            delete uploadStream;
            uploadStream = nullptr;
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadStream) {
            stm32u5_flash_lock();
            delete uploadStream;
            uploadStream = nullptr;
        }
        upload_status = 500;
        upload_result = "Upload aborted";
    }
}

void web_server_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connect failed.");
    }

    server.on("/", handle_index);
    server.on("/api/manifest", handle_manifest);
    server.on("/api/flash", HTTP_POST, handle_flash);
    
    server.on("/api/upload", HTTP_POST, []() {
        server.send(upload_status, "text/plain", upload_result);
    }, handleFileUpload);
    
    server.begin();
    Serial.println("Web server started.");
}

void web_server_process() {
    server.handleClient();
}
