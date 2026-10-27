#include "renderer.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RaidSim", __VA_ARGS__)

namespace {

const char* kVertexShader = R"(
attribute vec2 aPos;
uniform vec4 uRect; // x, y, width, height in clip space
void main() {
  gl_Position = vec4(uRect.xy + aPos * uRect.zw, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
precision mediump float;
uniform vec4 uColor;
void main() { gl_FragColor = uColor; }
)";

// Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1).
constexpr float kUnitQuad[] = {0, 0, 1, 0, 0, 1, 1, 1};
constexpr int kQuadVertexCount = 4;
constexpr int kComponentsPerVertex = 2;

// Companion button, relative to the shorter screen side.
constexpr float kButtonSizeFraction = 0.16f;
constexpr float kButtonMarginFraction = 0.35f;  // of the button size
// The magnifier icon on the button: a lens and a handle, relative to the button.
constexpr float kLensOffset = 0.22f;
constexpr float kLensSize = 0.42f;
constexpr float kHandleOffset = 0.6f;
constexpr float kHandleSize = 0.2f;
constexpr float kPressHighlightSeconds = 0.15f;
constexpr float kPressedBrightness = 0.75f;

constexpr Color kButtonColor{0.79f, 0.70f, 0.49f};  // the web UI's accent gold
constexpr Color kIconColor{0.1f, 0.1f, 0.1f};

// Background slowly "breathes" so a frozen frame is visible at a glance.
constexpr float kBackgroundPulseSpeed = 0.6f;

// Crates are laid out from fixed pseudo-random sequences so every run looks
// the same: start = frac(index * step), size and speed within a range.
constexpr float kCrateStartXStep = 0.37f;
constexpr float kCrateStartYStep = 0.61f;
constexpr float kCrateMinSpeedX = 0.05f;
constexpr float kCrateSpeedXRange = 0.12f;
constexpr float kCrateSpeedXStep = 0.13f;
constexpr float kCrateMinSpeedY = 0.04f;
constexpr float kCrateSpeedYRange = 0.10f;
constexpr float kCrateSpeedYStep = 0.29f;
constexpr float kCrateMinSize = 0.05f;
constexpr float kCrateSizeRange = 0.06f;
constexpr float kCrateSizeStep = 0.07f;
constexpr float kCrateMinShade = 0.25f;
constexpr float kCrateShadeRange = 0.35f;
constexpr float kCrateShadeStep = 0.17f;

// Fractional part of `index * step`, wrapped into [0, range).
float sequence(float index, float step, float range = 1.f) { return std::fmod(index * step, range); }

GLuint compileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    LOGE("shader compile failed: %s", log);
  }

  return shader;
}

Color crateColor(float shade) {
  return {0.35f * shade + 0.2f, 0.3f * shade + 0.18f, 0.2f * shade + 0.1f};
}

}  // namespace

bool Renderer::init() {
  GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
  GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
  m_program = glCreateProgram();
  glAttachShader(m_program, vertexShader);
  glAttachShader(m_program, fragmentShader);
  glLinkProgram(m_program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  GLint linked = 0;
  glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
  if (!linked) {
    LOGE("program link failed");
    return false;
  }

  m_positionAttribute = glGetAttribLocation(m_program, "aPos");
  m_rectUniform = glGetUniformLocation(m_program, "uRect");
  m_colorUniform = glGetUniformLocation(m_program, "uColor");

  glGenBuffers(1, &m_quadBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, m_quadBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuad), kUnitQuad, GL_STATIC_DRAW);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (size_t index = 0; index < m_crates.size(); ++index) {
    float position = static_cast<float>(index);
    m_crates[index] = {
        sequence(position, kCrateStartXStep),
        sequence(position, kCrateStartYStep),
        kCrateMinSpeedX + sequence(position, kCrateSpeedXStep, kCrateSpeedXRange),
        kCrateMinSpeedY + sequence(position, kCrateSpeedYStep, kCrateSpeedYRange),
        kCrateMinSize + sequence(position, kCrateSizeStep, kCrateSizeRange),
        kCrateMinShade + sequence(position, kCrateShadeStep, kCrateShadeRange),
    };
  }

  return true;
}

void Renderer::resize(int width, int height) {
  m_width = std::max(1, width);
  m_height = std::max(1, height);
  glViewport(0, 0, m_width, m_height);
  float buttonSize = kButtonSizeFraction * static_cast<float>(std::min(m_width, m_height));
  float margin = buttonSize * kButtonMarginFraction;
  m_button = {static_cast<float>(m_width) - buttonSize - margin, static_cast<float>(m_height) - buttonSize - margin,
             buttonSize, buttonSize};
}

void Renderer::pressButton() { m_pressHighlightSeconds = kPressHighlightSeconds; }

void Renderer::update(float deltaSeconds) {
  m_elapsedSeconds += deltaSeconds;
  m_pressHighlightSeconds = std::max(0.f, m_pressHighlightSeconds - deltaSeconds);
  for (auto& crate : m_crates) {
    crate.x += crate.velocityX * deltaSeconds;
    crate.y += crate.velocityY * deltaSeconds;
    if (crate.x < 0 || crate.x + crate.size > 1) {
      crate.velocityX = -crate.velocityX;
    }

    if (crate.y < 0 || crate.y + crate.size > 1) {
      crate.velocityY = -crate.velocityY;
    }

    crate.x = std::clamp(crate.x, 0.f, 1.f - crate.size);
    crate.y = std::clamp(crate.y, 0.f, 1.f - crate.size);
  }
}

void Renderer::drawQuad(const Rect& rect, const Color& color) {
  // Pixels (top-left origin) to clip space [-1, 1] (bottom-left origin).
  const float screenWidth = static_cast<float>(m_width);
  const float screenHeight = static_cast<float>(m_height);
  float clipX = rect.x / screenWidth * 2.f - 1.f;
  float clipY = 1.f - (rect.y + rect.height) / screenHeight * 2.f;
  glUniform4f(m_rectUniform, clipX, clipY, rect.width / screenWidth * 2.f, rect.height / screenHeight * 2.f);
  glUniform4f(m_colorUniform, color.red, color.green, color.blue, color.alpha);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, kQuadVertexCount);
}

void Renderer::draw() {
  float pulse = 0.5f + 0.5f * std::sin(m_elapsedSeconds * kBackgroundPulseSpeed);
  glClearColor(0.07f + 0.03f * pulse, 0.08f + 0.02f * pulse, 0.07f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(m_program);
  glBindBuffer(GL_ARRAY_BUFFER, m_quadBuffer);
  glEnableVertexAttribArray(static_cast<GLuint>(m_positionAttribute));
  glVertexAttribPointer(static_cast<GLuint>(m_positionAttribute), kComponentsPerVertex, GL_FLOAT, GL_FALSE, 0,
                        nullptr);

  const float screenWidth = static_cast<float>(m_width);
  const float screenHeight = static_cast<float>(m_height);
  const float shorterSide = std::min(screenWidth, screenHeight);
  for (const auto& crate : m_crates) {
    float side = crate.size * shorterSide;
    Rect rect{crate.x * (screenWidth - side), crate.y * (screenHeight - side), side, side};
    drawQuad(rect, crateColor(crate.shade));
  }

  float brightness = m_pressHighlightSeconds > 0 ? kPressedBrightness : 1.f;
  drawQuad(m_button, {kButtonColor.red * brightness, kButtonColor.green * brightness, kButtonColor.blue * brightness});
  Rect lens{m_button.x + m_button.width * kLensOffset, m_button.y + m_button.height * kLensOffset,
            m_button.width * kLensSize, m_button.height * kLensSize};
  drawQuad(lens, kIconColor);
  Rect handle{m_button.x + m_button.width * kHandleOffset, m_button.y + m_button.height * kHandleOffset,
              m_button.width * kHandleSize, m_button.height * kHandleSize};
  drawQuad(handle, kIconColor);
}

void Renderer::release() {
  if (m_quadBuffer) {
    glDeleteBuffers(1, &m_quadBuffer);
  }

  if (m_program) {
    glDeleteProgram(m_program);
  }

  m_quadBuffer = 0;
  m_program = 0;
}
