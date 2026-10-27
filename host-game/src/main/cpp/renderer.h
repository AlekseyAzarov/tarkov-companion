// A minimal game for the sample: moving crates and a Companion button, GLES2 quads.
#pragma once

#include <GLES2/gl2.h>

#include <array>

struct Rect {
  float x, y, width, height;  // pixels, origin top-left
  bool contains(float pointX, float pointY) const {
    return pointX >= x && pointX <= x + width && pointY >= y && pointY <= y + height;
  }
};

struct Color {
  float red, green, blue, alpha = 1.f;
};

class Renderer {
 public:
  bool init();
  void resize(int width, int height);
  void update(float deltaSeconds);
  void draw();
  void release();

  // Hit-test in window pixels.
  bool companionButtonHit(float x, float y) const { return m_button.contains(x, y); }
  void pressButton();

 private:
  void drawQuad(const Rect& rect, const Color& color);

  struct Crate {
    // Position and size are fractions of the screen; velocity in fractions per second.
    float x, y, velocityX, velocityY, size, shade;
  };

  GLuint m_program = 0;
  GLuint m_quadBuffer = 0;
  GLint m_rectUniform = -1;
  GLint m_colorUniform = -1;
  GLint m_positionAttribute = -1;
  int m_width = 1;
  int m_height = 1;
  float m_elapsedSeconds = 0;
  float m_pressHighlightSeconds = 0;
  Rect m_button{};
  std::array<Crate, 14> m_crates{};
};
