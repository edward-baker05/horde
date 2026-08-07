#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <stdexcept>
#include <vector>

class Circle {
    int x_;
    int y_;
    int r_;
    int filled_;
    int size_ = 0;
    int mass = 10;
    std::vector<SDL_FPoint> points_;
    std::vector<std::array<int, 4>> lines_;

    bool isColliding(const Circle&) const;
    void createPointsFilled();
    void createPointsOutline();

public:
    Circle(int, int, int, bool);

    void draw(SDL_Renderer*, int, int, int);
};

Circle::Circle(int x, int y, int r, bool filled) : x_(x), y_(y), r_(r), filled_(filled) {
    if (r > 200)
        throw std::invalid_argument("Radius given larger than maximum acceptable radius `200`");

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

// void Circle::createPointsFilled() {
//     for (int i = 0; i < 2 * r_; i++) {
//         for (int j = 0; j < 2 * r_; j++) {
//             int deltaX = i - r_;
//             int deltaY = j - r_;
//             if (std::sqrt(deltaX * deltaX + deltaY * deltaY) <= r_) {
//                 SDL_FPoint tempPoint;
//                 tempPoint.x = x_ + i - r_;
//                 tempPoint.y = y_ + j - r_;
//                 points.push_back(tempPoint);
//                 size_++;
//             }
//         }
//     }
// }

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
