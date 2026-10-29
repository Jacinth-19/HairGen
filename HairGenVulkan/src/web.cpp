// web.cpp - a small blocking HTTP server so the renderer can be driven from a
// browser: MJPEG preview stream, JSON parameter updates, PNG snapshots and the
// control panel page.
#include "web.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "png.hpp"

namespace hgen {
namespace {

std::string read_file(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return std::string();
  std::string out;
  char buf[8192];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

void send_all(int fd, const void* data, size_t size) {
  const char* p = static_cast<const char*>(data);
  while (size > 0) {
    ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
    if (n <= 0) return;
    p += n;
    size -= size_t(n);
  }
}

std::string http_headers(const char* status, const char* content_type, size_t length,
                         const char* extra = "") {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
           "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n%s\r\n",
           status, content_type, length, extra);
  return buf;
}

// Minimal JSON number extractor: finds "key": value in a flat object.
bool json_number(const std::string& body, const char* key, float& out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  out = float(std::atof(body.c_str() + p + 1));
  return true;
}

bool json_bool(const std::string& body, const char* key, bool& out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  size_t q = body.find_first_not_of(" \t\r\n", p + 1);
  if (q == std::string::npos) return false;
  out = (body.compare(q, 4, "true") == 0);
  return true;
}

// Material presets from the demo (SetHairMaterial() in hairmesh.js).
struct HairPreset {
  const char* name;
  float a_f[3];
};

const HairPreset kHairPresets[] = {
    {"std_blond", {0.507475266f, 0.465571405f, 0.394347166f}},
    {"mid_blond", {0.610475266f, 0.498571405f, 0.294347166f}},
    {"rosa", {0.707475266f, 0.385571405f, 0.344347166f}},
    {"brown", {0.347475266f, 0.245571405f, 0.164347166f}},
};

// Finds "key":"value" in a flat JSON object.
bool json_string(const std::string& body, const char* key, std::string& out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  size_t q = body.find('"', p + 1);
  if (q == std::string::npos) return false;
  size_t r = body.find('"', q + 1);
  if (r == std::string::npos) return false;
  out = body.substr(q + 1, r - q - 1);
  return true;
}

void apply_params(Scene& s, const std::string& body) {
  float f;
  bool b;
  std::string str;
  if (json_string(body, "style", str)) {
    for (const HairPreset& preset : kHairPresets) {
      if (str == preset.name) {
        for (int i = 0; i < 3; ++i) s.hair.a_f[i] = preset.a_f[i];
        break;
      }
    }
  }
  if (json_number(body, "rot", f)) s.camera.rot = f;
  if (json_number(body, "tilt", f)) s.camera.tilt = f;
  if (json_number(body, "dist", f)) s.camera.dist = f;
  if (json_number(body, "fov", f)) s.camera.fov = f;
  if (json_number(body, "sunRot", f)) s.sky.sun_rot = f;
  if (json_number(body, "sunTilt", f)) s.sky.sun_tilt = f;
  if (json_number(body, "sunIntensity", f)) s.sky.sun_intensity = f;
  if (json_number(body, "exposure", f)) s.sky.exposure = f;
  if (json_number(body, "bloom", f)) s.sky.bloom = f;
  if (json_number(body, "clouds", f)) s.sky.cloud_coverage = f;
  if (json_number(body, "vignette", f)) s.sky.vignette = f;
  if (json_number(body, "density", f)) s.hair.density = f;
  if (json_number(body, "lengthVar", f)) s.hair.length_var = f;
  if (json_number(body, "fuzz", f)) s.hair.fuzz_amount = f;
  if (json_number(body, "fuzzDistrib", f)) s.hair.fuzz_distrib = f;
  if (json_number(body, "frizz", f)) s.hair.frizz_amount = f;
  if (json_number(body, "frizzFreq", f)) s.hair.frizz_freq = f;
  if (json_number(body, "kink", f)) s.hair.kink_amount = f;
  if (json_number(body, "kinkFreqUV", f)) s.hair.kink_freq_uv = f;
  if (json_number(body, "kinkFreqW", f)) s.hair.kink_freq_w = f;
  if (json_number(body, "kinkStretch", f)) s.hair.kink_w_scale = f;
  if (json_number(body, "clump", f)) s.hair.clump_percent = f;
  if (json_number(body, "clumpSize", f)) s.hair.clump_size = f;
  if (json_number(body, "clumpVar", f)) s.hair.clump_var = f;
  if (json_number(body, "curl", f)) s.hair.curl_amount = f;
  if (json_number(body, "curlFreq", f)) s.hair.curl_freq = f;
  if (json_number(body, "curlTwists", f)) s.hair.curl_twists = f;
  if (json_number(body, "widthPx", f)) s.hair.width_px = f;
  if (json_number(body, "hairCount", f)) s.hair.hair_count = int(f);
  if (json_number(body, "red", f)) s.hair.a_f[0] = f;
  if (json_number(body, "green", f)) s.hair.a_f[1] = f;
  if (json_number(body, "blue", f)) s.hair.a_f[2] = f;
  // exponent / shape controls
  if (json_number(body, "fuzzExp", f)) s.hair.fuzz_exp = f;
  if (json_number(body, "frizzExp", f)) s.hair.frizz_exp = f;
  if (json_number(body, "kinkExp", f)) s.hair.kink_exp = f;
  if (json_number(body, "clumpExp", f)) s.hair.clump_exp = f;
  if (json_number(body, "clumpNoise", f)) s.hair.clump_noise = f;
  if (json_number(body, "clumpFreq", f)) s.hair.clump_freq = f;
  if (json_number(body, "curlExp", f)) s.hair.curl_exp = f;
  if (json_number(body, "curlTwistExp", f)) s.hair.curl_twist_exp = f;
  if (json_number(body, "ambient", f)) s.hair.ambient = f;
  if (json_number(body, "sunRot", f)) s.sky.sun_rot = f;
  if (json_bool(body, "showHair", b)) s.hair.show_hair = b;
  if (json_bool(body, "showHead", b)) s.hair.show_head = b;
  if (json_bool(body, "showGround", b)) s.sky.show_ground = b;
  // demo fidelity controls: render mode, explicit roots, collider push-out,
  // and the "show hair mesh" cage overlay.
  if (json_number(body, "renderMode", f)) s.hair.render_mode = int(f);
  if (json_bool(body, "rootSamples", b)) s.hair.use_root_samples = b;
  if (json_bool(body, "collide", b)) s.hair.collide = b;
  if (json_bool(body, "showHairMesh", b)) s.hair.show_hairmesh = b;
}

}  // namespace

// Shared between the accept loop and every live connection: the scene can be
// edited while a stream is running, and rendering is serialised because the
// Vulkan objects are not thread safe.
struct Session {
  std::mutex scene_mutex;   // guards `scene`
  std::mutex render_mutex;  // guards the Renderer + readback buffer
  Scene scene;
  uint32_t frame = 0;
  double time = 0.0;
  std::atomic<int> live_streams{0};
};

struct WebServer::Impl {
  Gpu& gpu;
  const Assets& assets;
  Renderer& renderer;
  int port;
  int listen_fd = -1;
  Session session;
  std::string base_dir = "assets/web";
};

WebServer::WebServer(Gpu& gpu, const Assets& assets, Renderer& renderer, int port) {
  impl_ = new Impl{gpu, assets, renderer, port};
}

WebServer::~WebServer() {
  if (impl_->listen_fd >= 0) ::close(impl_->listen_fd);
  delete impl_;
}

namespace {

// Reads one request (headers + body) from a socket.
bool read_request(int cfd, std::string& req, std::string& body) {
  char buf[8192];
  ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
  if (n <= 0) return false;
  req.assign(buf, size_t(n));
  size_t header_end = req.find("\r\n\r\n");
  while (header_end == std::string::npos && req.size() < 1024 * 1024) {
    n = ::recv(cfd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    req.append(buf, size_t(n));
    header_end = req.find("\r\n\r\n");
  }
  body.clear();
  if (header_end != std::string::npos) {
    body = req.substr(header_end + 4);
    size_t cl = 0;
    size_t p = req.find("Content-Length:");
    if (p != std::string::npos) cl = size_t(std::atol(req.c_str() + p + 15));
    while (body.size() < cl) {
      n = ::recv(cfd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      body.append(buf, size_t(n));
    }
  }
  return true;
}

std::string request_path(const std::string& req) {
  if (req.compare(0, 4, "GET ") == 0 || req.compare(0, 5, "POST ") == 0) {
    size_t start = req.find(' ') + 1;
    size_t end = req.find(' ', start);
    std::string path = req.substr(start, end - start);
    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);
    return path;
  }
  return "/";
}

void send_text(int cfd, const char* status, const char* type, const std::string& body) {
  std::string h = http_headers(status, type, body.size());
  send_all(cfd, h.data(), h.size());
  send_all(cfd, body.data(), body.size());
}

// Renders one frame with the current scene and returns it as PNG bytes.
std::vector<uint8_t> render_frame(WebServer::Impl& s, bool advance_time) {
  Scene scene;
  {
    std::lock_guard<std::mutex> lock(s.session.scene_mutex);
    scene = s.session.scene;
  }
  std::lock_guard<std::mutex> lock(s.session.render_mutex);
  s.renderer.set_scene(scene);
  if (advance_time) {
    s.session.time += 1.0 / 30.0;
    if (s.session.time > 1e6) s.session.time = 0.0;
  }
  s.renderer.render(s.session.time, s.session.frame++);
  return encode_png_rgba(s.renderer.width(), s.renderer.height(), s.renderer.read_pixels());
}

}  // namespace

int WebServer::run() {
  Impl& s = *impl_;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(uint16_t(s.port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  ::listen(fd, 16);
  s.listen_fd = fd;
  s.session.scene = s.renderer.scene();

  // The page is looked up relative to the working directory, the executable and
  // the source tree so the binary runs from anywhere.
  const char* dirs[] = {"assets/web", "../assets/web", "HairGenVulkan/assets/web"};
  for (const char* d : dirs) {
    if (!read_file(std::string(d) + "/index.html").empty()) {
      s.base_dir = d;
      break;
    }
  }

  printf("[hairgen] preview server on http://0.0.0.0:%d  (open the preview panel)\n", s.port);
  fflush(stdout);

  while (true) {
    sockaddr_in client{};
    socklen_t clen = sizeof(client);
    int cfd = ::accept(fd, reinterpret_cast<sockaddr*>(&client), &clen);
    if (cfd < 0) continue;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    Impl* sp = &s;
    std::thread([cfd, sp]() {
      Impl& s = *sp;
      std::string req, body;
      if (!read_request(cfd, req, body)) {
        ::close(cfd);
        return;
      }
      const std::string path = request_path(req);

      if (path == "/" || path == "/index.html") {
        std::string html = read_file(s.base_dir + "/index.html");
        if (html.empty())
          html = "<html><body><h1>HairGenVulkan</h1><p>assets/web/index.html missing</p></body></html>";
        send_text(cfd, "200 OK", "text/html; charset=utf-8", html);
      } else if (path == "/style.css" || path == "/app.js") {
        std::string content = read_file(s.base_dir + path);
        send_text(cfd, "200 OK", path == "/style.css" ? "text/css" : "application/javascript", content);
      } else if (path == "/params") {
        {
          std::lock_guard<std::mutex> lock(s.session.scene_mutex);
          apply_params(s.session.scene, body);
        }
        send_text(cfd, "200 OK", "application/json", "{\"ok\":true}");
      } else if (path == "/scene") {
        Scene scene;
        {
          std::lock_guard<std::mutex> lock(s.session.scene_mutex);
          scene = s.session.scene;
        }
        char json[1024];
        snprintf(json, sizeof(json),
                 "{\"rot\":%.4f,\"tilt\":%.4f,\"dist\":%.4f,\"fuzz\":%.4f,"
                 "\"clump\":%.4f,\"curl\":%.4f,\"width\":%d,\"height\":%d,"
                 "\"renderMode\":%d,\"rootSamples\":%s,\"collide\":%s,\"showHairMesh\":%s}",
                 scene.camera.rot, scene.camera.tilt, scene.camera.dist, scene.hair.fuzz_amount,
                 scene.hair.clump_percent, scene.hair.curl_amount, s.renderer.width(),
                 s.renderer.height(), scene.hair.render_mode,
                 scene.hair.use_root_samples ? "true" : "false", scene.hair.collide ? "true" : "false",
                 scene.hair.show_hairmesh ? "true" : "false");
        send_text(cfd, "200 OK", "application/json", json);
      } else if (path == "/snapshot.png") {
        std::vector<uint8_t> data = render_frame(s, false);
        std::string h = http_headers("200 OK", "image/png", data.size());
        send_all(cfd, h.data(), h.size());
        send_all(cfd, data.data(), data.size());
      } else if (path == "/stream") {
        const char* head =
            "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=hgenframe\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
        send_all(cfd, head, std::strlen(head));
        ++s.session.live_streams;
        while (true) {
          std::vector<uint8_t> png = render_frame(s, true);
          char part[192];
          snprintf(part, sizeof(part),
                   "--hgenframe\r\nContent-Type: image/png\r\nContent-Length: %zu\r\n\r\n",
                   png.size());
          send_all(cfd, part, std::strlen(part));
          send_all(cfd, png.data(), png.size());
          send_all(cfd, "\r\n", 2);
          // Stop when the browser goes away.
          fd_set rfds;
          FD_ZERO(&rfds);
          FD_SET(cfd, &rfds);
          timeval tv{0, 0};
          int ready = ::select(cfd + 1, &rfds, nullptr, nullptr, &tv);
          if (ready > 0) {
            char tmpbuf[4096];
            ssize_t got = ::recv(cfd, tmpbuf, sizeof(tmpbuf), MSG_DONTWAIT);
            if (got <= 0) break;
          }
        }
        --s.session.live_streams;
      } else {
        send_text(cfd, "404 Not Found", "text/plain", "not found");
      }
      ::close(cfd);
    }).detach();
  }
  return 0;
}

}  // namespace hgen
