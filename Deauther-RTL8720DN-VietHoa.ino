#include "vector"
#include "wifi_conf.h"
#include "map"
#include "wifi_cust_tx.h"
#include "wifi_util.h"
#include "wifi_structures.h"
#include "debug.h"
#include "WiFi.h"
#include "WiFiServer.h"
#include "WiFiClient.h"

// LEDs:
// LED_R (Đỏ): Hệ thống sẵn sàng, máy chủ web đang hoạt động,...
// LED_G (Xanh lá): Đang có giao tiếp từ máy chủ web (Web Server communication)
// LED_B (Xanh dương): Đang gửi gói Deauth (Deauth-Frame being sent)

typedef struct {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  short rssi;
  uint8_t channel;
} WiFiScanResult;

char *ssid = "FPT Wifi 6";
char *pass = "12345678@";
int current_channel = 1;
std::vector<WiFiScanResult> scan_results;
std::map<int, std::vector<int>> deauth_channels;
std::vector<int> chs_idx;
uint32_t current_ch_idx = 0;
uint32_t sent_frames = 0;
WiFiServer server(80);
uint8_t deauth_bssid[6];
uint16_t deauth_reason = 2;
int frames_per_deauth = 5;
int send_delay = 5;
bool isDeauthing = false;
bool led = true;

rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  rtw_scan_result_t *record;
  if (scan_result->scan_complete == 0) {
    record = &scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0;
    WiFiScanResult result;
    result.ssid = String((const char *)record->SSID.val);
    result.channel = record->channel;
    result.rssi = record->signal_strength;
    memcpy(&result.bssid, &record->BSSID, 6);
    char bssid_str[] = "XX:XX:XX:XX:XX:XX";
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X", result.bssid[0], result.bssid[1], result.bssid[2], result.bssid[3], result.bssid[4], result.bssid[5]);
    result.bssid_str = bssid_str;
    scan_results.push_back(result);
  }
  return RTW_SUCCESS;
}

int scanNetworks() {
  DEBUG_SER_PRINT("Scanning WiFi networks (5s)...");
  scan_results.clear();
  if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
    delay(5000);
    DEBUG_SER_PRINT(" done!\n");
    return 0;
  } else {
    DEBUG_SER_PRINT(" failed!\n");
    return 1;
  }
}

String parseRequest(String request) {
  int path_start = request.indexOf(' ');
  if (path_start < 0) return "/";
  path_start += 1;
  int path_end = request.indexOf(' ', path_start);
  if (path_end < 0) return "/";
  return request.substring(path_start, path_end);
}

std::vector<std::pair<String, String>> parsePost(String &request) {
  std::vector<std::pair<String, String>> post_params;
  int body_start = request.indexOf("\r\n\r\n");
  if (body_start == -1) return post_params;
  body_start += 4;
  String post_data = request.substring(body_start);
  int start = 0;
  int end = post_data.indexOf('&', start);
  while (end != -1) {
    String key_value_pair = post_data.substring(start, end);
    int delimiter_position = key_value_pair.indexOf('=');
    if (delimiter_position != -1) {
      String key = key_value_pair.substring(0, delimiter_position);
      String value = key_value_pair.substring(delimiter_position + 1);
      post_params.push_back({key, value});
    }
    start = end + 1;
    end = post_data.indexOf('&', start);
  }
  String key_value_pair = post_data.substring(start);
  int delimiter_position = key_value_pair.indexOf('=');
  if (delimiter_position != -1) {
    String key = key_value_pair.substring(0, delimiter_position);
    String value = key_value_pair.substring(delimiter_position + 1);
    post_params.push_back({key, value});
  }
  return post_params;
}

String makeResponse(int code, String content_type) {
  String response = "HTTP/1.1 " + String(code) + " OK\r\n";
  response += "Content-Type: " + content_type + "; charset=utf-8\r\n";
  response += "Connection: close\r\n\r\n";
  return response;
}

String makeRedirect(String url) {
  String response = "HTTP/1.1 307 Temporary Redirect\r\n";
  response += "Location: " + url + "\r\n\r\n";
  return response;
}

void handleRoot(WiFiClient &client) {
  String response = makeResponse(200, "text/html") + R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Hệ thống quét và gửi các gói tin đến mạng Wifi</title>
      <style>
          body {
              font-family: Arial, sans-serif;
              line-height: 1.6;
              color: #333;
              max-width: 800px;
              margin: 0 auto;
              padding: 20px;
              background-color: #f4f4f4;
          } 
          h1, h2 {
              color: #2c3e50;
          }
          table {
              width: 100%;
              border-collapse: collapse;
              margin-bottom: 20px;
          }
          th, td {
              padding: 12px;
              text-align: left;
              border-bottom: 1px solid #ddd;
          }
          th {
              background-color: #3498db;
              color: white;
          }
          tr:nth-child(even) {
              background-color: #f2f2f2;
          }
          form {
              background-color: white;
              padding: 20px;
              border-radius: 5px;
              box-shadow: 0 2px 5px rgba(0,0,0,0.1);
              margin-bottom: 20px;
          }
          input[type="submit"] {
              padding: 10px 20px;
              border: none;
              background-color: #3498db;
              color: white;
              border-radius: 4px;
              cursor: pointer;
              transition: background-color 0.3s;
          }
          input[type="submit"]:hover {
              background-color: #2980b9;
          }
      </style>
  </head>
  <body>
      <h1>Hệ thống quét và gửi các gói tin đến mạng Wifi</h1>
      <h1>Khi cần hỗ trợ hãy liên hệ Zalo 0812.40.9939</h1>
      <h2 style="color: #FF0000; font-size: 24px; text-align: center;">HỆ THỐNG WIFI ĐÃ ĐƯỢC TÌM THẤY</h2>
          <form method="post" action="/notify_deauth">
          <table>
              <tr>
                  <th>Tích để chọn</th>
                  <th>Stt</th>
                  <th>Tên mạng Wifi</th>
                  <th>Địa chỉ MAC</th>
                  <th>Kênh</th>
                  <th>Tín hiệu</th>
                  <th>Phân loại mạng 2.4 Ghz & 5 Ghz</th>
              </tr>
  )rawliteral";

  for (uint32_t i = 0; i < scan_results.size(); i++) {
    response += "<tr>";
    response += "<td><input type='checkbox' name='network' value='" + String(i) + "'></td>";
    response += "<td>" + String(i) + "</td>";
    response += "<td>" + scan_results[i].ssid + "</td>";
    response += "<td>" + scan_results[i].bssid_str + "</td>";
    response += "<td>" + String(scan_results[i].channel) + "</td>";
    response += "<td>" + String(scan_results[i].rssi) + "</td>";
    response += "<td>" + (String)((scan_results[i].channel >= 36) ? "5GHz" : "2.4GHz") + "</td>";
    response += "</tr>";
  }

  response += R"rawliteral(
        </table>
          <p>Nhập mã để tấn công ( Mặc định là 2):</p>
          <input type="text" name="reason" value="2">
          <input type="submit" value="💥Bấm vào đây để gửi lệnh tấn công💥">
      </form>
      <h2>Bảng điều khiển</h2>
    <table>
      <tr><th>Tên cấu hình </th><th>Giá trị</th></tr>
  )rawliteral";

  response += "<tr><td>🕵️‍♂️Trạng thái tấn công =>>🕵️‍♂️ </td><td>" + String(isDeauthing ? "Đang tấn công " : "Đang ngừng") + "</th></tr>";
  response += "<tr><td>Trạng thái đèn LED</td><td>" + String(led ? "ON" : "OFF") + "</th></tr>";
  response += "<tr><td>📡Dữ liệu Deauthentication đã gửi:📡 (Bấm load lại trang để xem) </td><td>" + String(sent_frames) + "</th></tr>";
  response += "<tr><td>Send Delay</td><td>" + String(send_delay) + "</th></tr>";
  response += "<tr><td>Frames Per Deauth</td><td>" + String(frames_per_deauth) + "</th></tr>";

  response += R"rawliteral(
    </table>
      <h2>Cấu hình (Khúc dưới này bạn không cần điền gì đâu. Cứ để mặc định nhé.)</h2>
      <form method="post" action="/setframes">
          <input type="text" name="frames" placeholder="Frames per deauth">
          <input type="submit" value="Set frames per deauth">
      </form>
      <form method="post" action="/setdelay">
          <input type="text" name="delay" placeholder="Send delay">
          <input type="submit" value="Set send delay">
      </form>
      <form method="post" action="/rescan">
          <input type="submit" value="🔎Quét lại mạng🔎">
      </form>
      <form method="post" action="/stop">
          <input type="submit" value="🛑Ngừng tấn công🛑">
      </form>
      <form method="post" action="/led_enable">
          <input type="submit" value="LED enable">
      </form>
      <form method="post" action="/led_disable">
          <input type="submit" value="LED disable">
      </form>
      <form method="post" action="/refresh">
          <input type="submit" value="Làm mới lại trang">
      </form>
      <h2>Ý nghĩa mã lý do:</h2>
    <table>
      <tr><th>Mã</th><th>Ý nghĩa</th></tr>
      <tr><td>0</td><td>Reserved.</td></tr>
      <tr><td>1</td><td>Lý do không xác định.</td></tr>
      <tr><td>2</td><td>Xác thực trước đó không còn hợp lệ.</td></tr>
      <tr><td>3</td><td>Deauth do STA rời khỏi IBSS/ESS.</td></tr>
      <tr><td>4</td><td>Ngắt kết nối do không hoạt động.</td></tr>
      <tr><td>5</td><td>Không thể xử lý tất cả STA hiện tại.</td></tr>
      <tr><td>6</td><td>Frame lớp 2 từ STA chưa xác thực.</td></tr>
      <tr><td>7</td><td>Frame lớp 3 từ STA chưa kết nối.</td></tr>
      <tr><td>8</td><td>STA rời khỏi mạng BSS.</td></tr>
      <tr><td>9</td><td>Yêu cầu kết nối lại nhưng chưa xác thực.</td></tr>
      <tr><td>10</td><td>Power Capability không phù hợp.</td></tr>
      <tr><td>11</td><td>Supported Channels không phù hợp.</td></tr>
      <tr><td>12</td><td>Quản lý chuyển đổi BSS.</td></tr>
      <tr><td>13</td><td>Phần tử không hợp lệ theo IEEE 802.11.</td></tr>
      <tr><td>14</td><td>Message Integrity Code (MIC) thất bại.</td></tr>
      <tr><td>15</td><td>4-Way Handshake timeout.</td></tr>
      <tr><td>16</td><td>Group Key Handshake timeout.</td></tr>
      <tr><td>17</td><td>Khác biệt cipher suite so với yêu cầu.</td></tr>
      <tr><td>18</td><td>Bộ mã hóa nhóm không hợp lệ.</td></tr>
      <tr><td>19</td><td>Bộ mã hóa cá nhân không hợp lệ.</td></tr>
      <tr><td>20</td><td>Phương thức AKMP không hợp lệ.</td></tr>
      <tr><td>21</td><td>Phiên bản RSNE không được hỗ trợ.</td></tr>
      <tr><td>22</td><td>Khả năng RSNE không hợp lệ.</td></tr>
      <tr><td>23</td><td>Xác thực IEEE 802.1X thất bại.</td></tr>
      <tr><td>24</td><td>Cipher suite bị từ chối theo chính sách bảo mật.</td></tr>
    </table>
  </body>
  </html>
  )rawliteral";

  client.write(response.c_str());
}

void handle404(WiFiClient &client) {
  String response = makeResponse(404, "text/plain");
  response += "Not found!";
  client.write(response.c_str());
}

void setup() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  DEBUG_SER_INIT();
  WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
  scanNetworks();

#ifdef DEBUG
  for (uint i = 0; i < scan_results.size(); i++) {
    DEBUG_SER_PRINT(scan_results[i].ssid + " ");
    for (int j = 0; j < 6; j++) {
      if (j > 0) DEBUG_SER_PRINT(":");
      DEBUG_SER_PRINT(scan_results[i].bssid[j], HEX);
    }
    DEBUG_SER_PRINT(" " + String(scan_results[i].channel) + " ");
    DEBUG_SER_PRINT(String(scan_results[i].rssi) + "\n");
  }
#endif

  server.begin();
  if (led) {
    digitalWrite(LED_R, HIGH);
  }
}

void loop() {
  WiFiClient client = server.available();
  if (client.connected()) {
    if (led) {
      digitalWrite(LED_G, HIGH);
    }
    String request;
    while (client.available()) {
      request += (char)client.read();
    }
    DEBUG_SER_PRINT(request);
    String path = parseRequest(request);
    DEBUG_SER_PRINT("\nRequested path: " + path + "\n");

    if (path == "/") {
      handleRoot(client);
    } else if (path == "/rescan") {
      client.write(makeRedirect("/").c_str());
      scanNetworks();
    } else if (path == "/notify_deauth") {
      String response = makeResponse(200, "text/html") + R"rawliteral(
        <script>
          alert("Đây là phiên bản thử nghiệm để sử dụng đầy đủ tính năng vui lòng liên hệ Zalo : 0812.40.9939");
          window.location.href = "/";
        </script>
      )rawliteral";
      client.write(response.c_str());
    } else if (path == "/setframes") {
      std::vector<std::pair<String, String>> post_data = parsePost(request);
      for (auto &param : post_data) {
        if (param.first == "frames") {
          int frames = String(param.second).toInt();
          frames_per_deauth = frames <= 0 ? 5 : frames;
        }
      }
      client.write(makeRedirect("/").c_str());
    } else if (path == "/setdelay") {
      std::vector<std::pair<String, String>> post_data = parsePost(request);
      for (auto &param : post_data) {
        if (param.first == "delay") {
          int delay = String(param.second).toInt();
          send_delay = delay <= 0 ? 5 : delay;
        }
      }
      client.write(makeRedirect("/").c_str());
    } else if (path == "/stop") {
      deauth_channels.clear();
      chs_idx.clear();
      isDeauthing = false;
      client.write(makeRedirect("/").c_str());
    } else if (path == "/led_enable") {
      led = true;
      digitalWrite(LED_R, HIGH);
      client.write(makeRedirect("/").c_str());
    } else if (path == "/led_disable") {
      led = false;
      digitalWrite(LED_R, LOW);
      digitalWrite(LED_G, LOW);
      digitalWrite(LED_B, LOW);
      client.write(makeRedirect("/").c_str());
    } else if (path == "/refresh") {
      client.write(makeRedirect("/").c_str());
    } else {
      handle404(client);
    }
    client.stop();
    if (led) {
      digitalWrite(LED_G, LOW);
    }
  }

  wext_set_channel(WLAN0_NAME, current_channel);
}
