#include <cmath>
#include <stdexcept>

#include "SDL3/SDL_rect.h"

class Circle {
    int r_;
    int x_ = 0;
    int y_ = 0;
    bool is_colliding(const Circle& collider) const;
    void createPoints();

public:
    SDL_FPoint points[4000];
    Circle(int r, int x = 0, int y = 0);

    int getX() const {
        return x_;
    }

    int getY() const {
        return y_;
    }

    int getR() const {
        return r_;
    }

    void setX(int newX) {
        x_ = newX;
    }

    void setY(int newY) {
        y_ = newY;
    }
};

Circle::Circle(int r, int x, int y) : r_(r), x_(x), y_(y) {
    if (r > 200)
        throw std::invalid_argument("Radius given larger than maximum acceptable radius `200`");

    createPoints();
}

void Circle::createPoints() {
    for (int i = 0; i < 2 * r_; i++) {
        for (int j; j < 2 * r_; j++) {
            int deltaX = i - x_;
            int deltaY = j - y_;
            if (std::sqrt(deltaX * deltaX + deltaY * deltaY) <= r_) {
                points[i * j].x = x_ + i;
                points[i * j].y = y_ + j;
            }
        }
    }
}
