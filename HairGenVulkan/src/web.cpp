// web.cpp - a small blocking HTTP server so the renderer can be driven from a
// browser: MJPEG preview stream, JSON parameter updates, PNG snapshots and the
// control panel page.
#include "web.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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
}

}  // namespace

struct WebServer::Impl {
  Gpu& gpu;
  const Assets& assets;
  Renderer& renderer;
  int port;
  int listen_fd = -1;
};

WebServer::WebServer(Gpu& gpu, const Assets& assets, Renderer& renderer, int port) {
  impl_ = new Impl{gpu, assets, renderer, port};
}

WebServer::~WebServer() {
  if (impl_->listen_fd >= 0) ::close(impl_->listen_fd);
  delete impl_;
}

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
  printf("[hairgen] preview server on http://0.0.0.0:%d  (open the preview panel)\n", s.port);
  fflush(stdout);

  std::string base_dir = "assets/web";
  // Try a couple of locations so the binary works from the repo root or its own dir.
  if (read_file(base_dir + "/index.html").empty()) base_dir = "HairGenVulkan/assets/web";

  Scene scene = s.renderer.scene();
  uint32_t frame = 0;
  double t = 0.0;
  bool dirty = true;
  auto last = std::chrono::steady_clock::now();

  while (true) {
    sockaddr_in client{};
    socklen_t clen = sizeof(client);
    int cfd = ::accept(fd, reinterpret_cast<sockaddr*>(&client), &clen);
    if (cfd < 0) continue;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    // Read the request (headers + optional body).
    std::string req;
    char buf[8192];
    ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
    if (n <= 0) {
      ::close(cfd);
      continue;
    }
    req.append(buf, size_t(n));
    size_t header_end = req.find("\r\n\r\n");
    while (header_end == std::string::npos && req.size() < 1024 * 1024) {
      n = ::recv(cfd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      req.append(buf, size_t(n));
      header_end = req.find("\r\n\r\n");
    }
    std::string body;
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

    std::string path = "/";
    if (req.compare(0, 4, "GET ") == 0 || req.compare(0, 5, "POST ") == 0) {
      size_t start = req.find(' ') + 1;
      size_t end = req.find(' ', start);
      path = req.substr(start, end - start);
    }

    if (path == "/" || path == "/index.html") {
      std::string html = read_file(base_dir + "/index.html");
      if (html.empty()) html = "<html><body><h1>HairGenVulkan</h1><p>assets/web/index.html missing</p></body></html>";
      std::string h = http_headers("200 OK", "text/html; charset=utf-8", html.size());
      send_all(cfd, h.data(), h.size());
      send_all(cfd, html.data(), html.size());
      ::close(cfd);
      continue;
    }
    if (path == "/style.css" || path == "/app.js") {
      std::string content = read_file(base_dir + path);
      const char* type = path == "/style.css" ? "text/css" : "application/javascript";
      std::string h = http_headers("200 OK", type, content.size());
      send_all(cfd, h.data(), h.size());
      send_all(cfd, content.data(), content.size());
      ::close(cfd);
      continue;
    }
    if (path == "/params" && !body.empty()) {
      apply_params(scene, body);
      s.renderer.set_scene(scene);
      dirty = true;
      const char* ok = "{\"ok\":true}";
      std::string h = http_headers("200 OK", "application/json", std::strlen(ok));
      send_all(cfd, h.data(), h.size());
      send_all(cfd, ok, std::strlen(ok));
      ::close(cfd);
      continue;
    }
    if (path == "/snapshot.png") {
      s.renderer.set_scene(scene);
      s.renderer.render(t, frame++);
      auto px = s.renderer.read_pixels();
      // PNG encode in memory into a temp file for simplicity.
      std::string tmp = "/tmp/hairgen_snapshot.png";
      write_png_rgba(tmp, s.renderer.width(), s.renderer.height(), px);
      std::string data = read_file(tmp);
      std::string h = http_headers("200 OK", "image/png", data.size());
      send_all(cfd, h.data(), h.size());
      send_all(cfd, data.data(), data.size());
      ::close(cfd);
      continue;
    }
    if (path == "/stream") {
      const char* head =
          "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=hgenframe\r\n"
          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
      send_all(cfd, head, std::strlen(head));
      s.renderer.set_scene(scene);
      while (true) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        t += dt;
        (void)dt;
        s.renderer.render(t, frame);
        auto px = s.renderer.read_pixels();
        std::string tmp = "/tmp/hairgen_stream.png";
        write_png_rgba(tmp, s.renderer.width(), s.renderer.height(), px);
        std::string png = read_file(tmp);
        char part[256];
        snprintf(part, sizeof(part),
                 "--hgenframe\r\nContent-Type: image/png\r\nContent-Length: %zu\r\n\r\n", png.size());
        send_all(cfd, part, std::strlen(part));
        send_all(cfd, png.data(), png.size());
        send_all(cfd, "\r\n", 2);
        // Poll for a new request from the UI (non-blocking): if the client went
        // away or wants new params, stop streaming.
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
        (void)dirty;
      }
      ::close(cfd);
      continue;
    }

    const char* notfound = "not found";
    std::string h = http_headers("404 Not Found", "text/plain", std::strlen(notfound));
    send_all(cfd, h.data(), h.size());
    send_all(cfd, notfound, std::strlen(notfound));
    ::close(cfd);
  }
  return 0;
}

}  // namespace hgen
