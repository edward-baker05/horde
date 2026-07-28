#include <SFML/Graphics.hpp>

int main() {
    sf::RenderWindow window(sf::VideoMode({1920, 1080}), "Horde game!");
    sf::CircleShape shape(30);
    shape.setOutlineColor(sf::Color::Green);
    shape.setOutlineThickness(2);
    shape.setFillColor(sf::Color::Transparent);

    sf::Font font("arial.ttf");
    sf::Text text(font);
    text.setString("Hello Bailen!");
    text.setCharacterSize(34);

    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        window.clear();
        window.draw(shape);
        window.display();
    }
}
