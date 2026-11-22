#pragma once

namespace Rml {
class ElementDocument;
class Element;
} // namespace Rml

namespace horde::scene {

struct Services;

// The level scenes' vibe in Polish mode: the Polish cow song, and the dancing
// cow GIF bouncing round the whole window like an old DVD logo. In normal mode
// show() only silences the music.
//
// Each level scene (round, editor, dev ground) owns one and shows it while it
// is the top scene. The cow lives in RmlUi's window context, drawn over every
// device, and lets every click through to whatever is under it. RmlUi cannot
// play GIFs, so the GIF's frames are PNGs and update() steps through them.
class DancingCow {
public:
    ~DancingCow();

    // Starts the music (fading the previous track out first) and the cow.
    void show(Services& services);
    // Takes the cow away. The music is left to whichever scene comes next.
    void hide();
    // Advances the GIF.
    void update(float deltaTime);

private:
    Rml::ElementDocument* m_document = nullptr;
    Rml::Element* m_image = nullptr;
    float m_time = 0.0f;
    int m_frame = 0;
};

} // namespace horde::scene
