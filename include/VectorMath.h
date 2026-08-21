#pragma once

#include <SDL3/SDL.h>

#include <cmath>

// Marked as 'inline' so multiple .cpp files can include this header
// without causing "duplicate symbol" linker errors.

inline SDL_FPoint operator+(SDL_FPoint a, SDL_FPoint b) {
    return {a.x + b.x, a.y + b.y};
}

inline SDL_FPoint operator-(SDL_FPoint a, SDL_FPoint b) {
    return {a.x - b.x, a.y - b.y};
}

inline SDL_FPoint operator*(SDL_FPoint p, float scalar) {
    return {p.x * scalar, p.y * scalar};
}

inline SDL_FPoint operator*(float scalar, SDL_FPoint p) {
    return {p.x * scalar, p.y * scalar}; // Handles float * SDL_FPoint order
}

inline SDL_FPoint operator/(SDL_FPoint p, float scalar) {
    return {p.x / scalar, p.y / scalar};
}

inline SDL_FPoint& operator+=(SDL_FPoint& a, SDL_FPoint b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

inline SDL_FPoint& operator-=(SDL_FPoint& a, SDL_FPoint b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

// Utility math helpers
inline float SDL_FPointLength(SDL_FPoint p) {
    return std::sqrt(p.x * p.x + p.y * p.y);
}

inline SDL_FPoint SDL_FPointNormalize(SDL_FPoint p) {
    float len = SDL_FPointLength(p);
    if (len > 0.0001f) {
        return p / len;
    }
    return {0.0f, 0.0f};
}
