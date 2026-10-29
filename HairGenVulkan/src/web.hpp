// web.hpp - tiny built-in HTTP server: live MJPEG preview plus the control panel.
#pragma once

#include <string>

#include "renderer.hpp"

namespace hgen {

class WebServer {
 public:
  WebServer(Gpu& gpu, const Assets& assets, Renderer& renderer, int port);
  ~WebServer();
  int run();

  struct Impl;  // defined in web.cpp

 private:
  Impl* impl_;
};

}  // namespace hgen
