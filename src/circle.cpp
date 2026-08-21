#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "SDL3/SDL_rect.h"
#include "VectorMath.h"

class Circle {
    int x_;
    int y_;
    SDL_FPoint truePos;
    int r_;
    int filled_;
    int size_ = 0;
    int mass = 10;
    std::vector<SDL_FPoint> points_;
    std::vector<std::array<int, 4>> lines_;
    SDL_FPoint velocity;

    bool isColliding(const Circle&) const;
    void resolveCollision(Circle circle);
    void createPointsFilled();
    void createPointsOutline();

public:
    Circle(int, int, int, bool);

    void draw(SDL_Renderer*, int, int, int);
    void update(std::vector<Circle> circles, int selfIndex, float x, float y);

    void setVelocity(float x, float y) {
        velocity.x = x;
        velocity.y = y;
    }
};

Circle::Circle(int x, int y, int r, bool filled) : x_(x), y_(y), r_(r), filled_(filled) {
    if (r > 200)
        throw std::invalid_argument("Radius given larger than maximum acceptable radius `200`");

    truePos.x = x_;
    truePos.y = y_;

    if (filled_) {
        createPointsFilled();
    } else {
        createPointsOutline();
    }
}

void Circle::draw(SDL_Renderer* renderer, int r, int g, int b) {
    SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);

    if (filled_) {
        for (std::array<int, 4> line : lines_) {
            SDL_RenderLine(renderer, line[0], line[1], line[2], line[3]);
        }
    } else {
        SDL_RenderPoints(renderer, &points_[0], size_);
    }
}

void Circle::update(std::vector<Circle> circles, int selfIndex, float x = 0, float y = 0) {
    if (x == 0 && y == 0) {
        truePos += velocity;
    } else {
        truePos.x += x;
        truePos.y += y;
    }

    for (int i = 0; i < circles.size(); i++) {
        if (i == selfIndex)
            continue;
        if (isColliding(circles[i]))
            resolveCollision(circles[i]);
    }

    if (truePos.x - x_ >= 1 || truePos.y - y_ >= 1 || x_ - truePos.x >= 1 || y_ - truePos.y >= 1) {
        x_ = std::floor(truePos.x);
        y_ = std::floor(truePos.y);
        if (filled_) {
            createPointsFilled();
        } else {
            createPointsOutline();
        }
    }
}

bool Circle::isColliding return false;
}

void Circle::resolveCollision(Circle circle) {}

void Circle::createPointsFilled() {
    lines_.clear();
    size_ = 0;

    int x = 0;
    int y = r_;
    int d = 3 - 2 * r_;

    auto addPoint = [this](int px, int py) {
        lines_.push_back({x_ + px, y_ + py, x_ - px, y_ + py});
        size_++;
    };

    auto addSymmetricPoints = [&](int x, int y) {
        addPoint(x, y);
        addPoint(x, -y);
        addPoint(y, x);
        addPoint(y, -x);
    };

    addSymmetricPoints(x, y);

    while (y >= x) {
        x++;

        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }

        addSymmetricPoints(x, y);
    }
}

void Circle::createPointsOutline() {
    points_.clear();
    size_ = 0;

    int x = 0;
    int y = r_;
    int d = 3 - 2 * r_;

    auto addPoint = [this](float px, float py) {
        points_.push_back({px, py});
        size_++;
    };

    auto addSymmetricPoints = [&](int x, int y) {
        addPoint(x_ + x, y_ + y);
        addPoint(x_ - x, y_ + y);
        addPoint(x_ + x, y_ - y);
        addPoint(x_ - x, y_ - y);
        addPoint(x_ + y, y_ + x);
        addPoint(x_ - y, y_ + x);
        addPoint(x_ + y, y_ - x);
        addPoint(x_ - y, y_ - x);
    };

    addSymmetricPoints(x, y);

    while (y >= x) {
        x++;

        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }

        addSymmetricPoints(x, y);
    }
}
