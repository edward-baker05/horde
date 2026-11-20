#include "core/PolishMode.hpp"

#include <string_view>
#include <unordered_map>

namespace horde {

namespace {

bool g_polish = false;

// English UI text to Polish. A "##" suffix is ImGui's hidden ID: it lets one
// English word take two Polish forms ("Spawn" is a marker, "Spawn##pattern"
// a layout).
const std::unordered_map<std::string_view, const char*>& translations() {
    static const std::unordered_map<std::string_view, const char*> table = {
        {"%.0f deg", "%.0f°"},
        {"%.1f deg", "%.1f°"},
        {"%.1f fps: %.2f ms per frame (worst %.2f)", "%.1f kl./s: %.2f ms na klatkę (najgorzej %.2f)"},
        {"%.1f hp", "%.1f PŻ"},
        {"%.2f steps/frame", "%.2f kroku na klatkę"},
        {"%6.3f ms  (worst %.3f)", "%6.3f ms  (najgorzej %.3f)"},
        {"%d cells", "%d kom."},
        {"%s %zu: %d of its %d cells are painted over", "%s %zu: zamalowano %d z %d komórek"},
        {"%u passes x %.0f Hz = %.0f passes/s", "%u przejść × %.0f Hz = %.0f przejść/s"},
        {"%u passes, one step per frame", "%u przejść, jeden krok na klatkę"},
        {"%zu  Field region##field%zu", "%zu  Strefa pola##field%zu"},
        {"%zu points", "%zu punktów"},
        {"(none)", "(brak)"},
        {"0 flows east; positive turns clockwise.", "0 płynie na wschód; dodatni skręca zgodnie z zegarem."},
        {"Advanced", "Zaawansowane"},
        {"Armed (right-click to fire)", "Uzbrojony (prawy przycisk odpala)"},
        {"Auto-smooth slopes", "Wygładzaj skosy"},
        {"Back", "Wstecz"},
        {"Back to menu", "Powrót do menu"},
        {"Background", "Tło"},
        {"Blast damage", "Obrażenia wybuchu"},
        {"Blast duration", "Czas wybuchu"},
        {"Blast radius", "Promień wybuchu"},
        {"Blast strength", "Siła wybuchu"},
        {"Bounds", "Granice"},
        {"Brush", "Pędzel"},
        {"Busy per frame", "Praca na klatkę"},
        {"Centre", "Środek"},
        {"Chebyshev delay", "Opóźnienie Czebyszewa"},
        {"Circle", "Koło"},
        {"Collision passes", "Przejścia kolizji"},
        {"Colour", "Kolor"},
        {"Contents", "Zawartość"},
        {"Could not open ", "Nie można otworzyć "},
        {"Could not save: ", "Nie można zapisać: "},
        {"Could run at ~%.0f fps uncapped", "Bez limitu: ~%.0f kl./s"},
        {"Crowd flow", "Przepływ tłumu"},
        {"Damage at exit", "Obrażenia przy wyjściu"},
        {"Decay", "Zanik"},
        {"Delete", "Usuń"},
        {"Edge: %s", "Krawędź: %s"},
        {"Erase", "Zmaż"},
        {"Every level needs at least one spawn and one exit.", "Każdy poziom potrzebuje co najmniej jednego wejścia i jednego wyjścia."},
        {"Exit", "Wyjście"},
        {"Explosion", "Wybuch"},
        {"Falloff", "Spadek"},
        {"Field region", "Strefa pola"},
        {"Field region %zu", "Strefa pola %zu"},
        {"Field regions", "Strefy pola"},
        {"Files", "Pliki"},
        {"Fling (past max speed)", "Wyrzut (ponad maks. prędkość)"},
        {"Fling drag", "Opór wyrzutu"},
        {"Flow", "Przepływ"},
        {"Frame building, CPU", "Budowa klatki, CPU"},
        {"Frame stats", "Statystyki klatek"},
        {"Grid", "Siatka"},
        {"Health: mean %.1f / %u, lowest %.1f, %u despawned", "Zdrowie: średnio %.1f / %u, najniżej %.1f, zniknęło %u"},
        {"History", "Historia"},
        {"Hit points a unit at the centre loses over the blast, fading toward the edge\nby the falloff. Units left with none are despawned.", "Punkty życia, które traci jednostka w centrum wybuchu, malejąc ku krawędzi\nwedług spadku. Jednostki bez punktów życia znikają."},
        {"How quickly units turn to follow the arrow.", "Jak szybko jednostki skręcają za strzałką."},
        {"How the blast fades from its centre to its edge.", "Jak wybuch słabnie od centrum ku krawędzi."},
        {"How the blast fades over its duration: constant is even all the way,\nlinear and quadratic put more of it at the start.", "Jak wybuch słabnie w czasie: stały działa równo do końca,\nliniowy i kwadratowy skupiają więcej siły na początku."},
        {"Inspector", "Inspektor"},
        {"Level", "Poziom"},
        {"Level##dev", "Poligon"},
        {"Live", "Na żywo"},
        {"Markers", "Znaczniki"},
        {"Markers are placed on an edge and cannot be recoloured or rotated.", "Znaczniki leżą na krawędzi; nie można ich przekolorować ani obrócić."},
        {"Max HP", "Maks. PŻ"},
        {"Measure GPU physics", "Mierz fizykę GPU"},
        {"Name", "Nazwa"},
        {"New", "Nowy"},
        {"New field region", "Nowa strefa pola"},
        {"New level", "Nowy poziom"},
        {"New polyline", "Nowa łamana"},
        {"New wall colour", "Kolor nowej ściany"},
        {"No field regions yet.", "Brak stref pola."},
        {"No levels saved yet.", "Brak zapisanych poziomów."},
        {"No walls yet.", "Brak ścian."},
        {"None. This level can be saved.", "Brak. Poziom można zapisać."},
        {"Not supported by this physics system", "Ten system fizyki tego nie obsługuje"},
        {"Nothing selected.", "Nic nie zaznaczono."},
        {"Offset: %.1f   Length: %.1f", "Przesunięcie: %.1f   Długość: %.1f"},
        {"On restart", "Po restarcie"},
        {"Open", "Otwórz"},
        {"Opened ", "Otwarto "},
        {"Other (events, UI)", "Inne (zdarzenia, UI)"},
        {"Paint", "Maluj"},
        {"Paint cells", "Malowanie komórek"},
        {"Physics rate", "Częstotliwość fizyki"},
        {"Physics total", "Fizyka razem"},
        {"Physics, CPU", "Fizyka, CPU"},
        {"Physics, GPU", "Fizyka, GPU"},
        {"Polyline", "Łamana"},
        {"Positive rotation is clockwise on screen.", "Dodatni obrót idzie zgodnie z ruchem wskazówek zegara."},
        {"Preset", "Zestaw"},
        {"Problems", "Problemy"},
        {"Radius", "Promień"},
        {"Rectangle", "Prostokąt"},
        {"Redo", "Ponów"},
        {"Relaxation", "Relaksacja"},
        {"Reorder units by cell", "Sortuj jednostki według komórek"},
        {"Respawn at exit", "Odradzaj przy wyjściu"},
        {"Restart level", "Restartuj poziom"},
        {"Restart with", "Uruchom ponownie z"},
        {"Right drag does the opposite. Alt-click picks a colour.", "Prawy przycisk robi odwrotnie. Alt+klik pobiera kolor."},
        {"Rotation", "Obrót"},
        {"Save", "Zapisz"},
        {"Saved ", "Zapisano "},
        {"Select", "Zaznacz"},
        {"Shift-click: straight line. Shift+1-8: colour. [ ]: size.", "Shift+klik: prosta. Shift+1-8: kolor. [ ]: rozmiar."},
        {"Show field regions", "Pokaż strefy pola"},
        {"Size", "Rozmiar"},
        {"Slope", "Skos"},
        {"Slow down for crowds ahead", "Zwalniaj przed tłumem"},
        {"Snap position", "Przyciągaj pozycję"},
        {"Snap rotation", "Przyciągaj obrót"},
        {"Snapping", "Przyciąganie"},
        {"Solver", "Solwer"},
        {"Spawn", "Wejście"},
        {"Spawn rate", "Tempo pojawiania"},
        {"Spawn##pattern", "Rozmieszczenie"},
        {"Speed", "Prędkość"},
        {"Speed, in world units per second, a unit at the centre is launched at in all.\nNegative pulls units in.", "Prędkość (jednostki świata na sekundę), z jaką wylatuje jednostka w centrum.\nUjemna przyciąga jednostki."},
        {"Square", "Kwadrat"},
        {"Start (units/cell)", "Początek (jedn./komórkę)"},
        {"Status", "Stan"},
        {"Step", "Krok"},
        {"Stop (units/cell)", "Koniec (jedn./komórkę)"},
        {"Strength", "Siła"},
        {"Tech tree", "Drzewko technologii"},
        {"Test play", "Gra testowa"},
        {"These don't stop saving.", "Nie blokują zapisu."},
        {"Thickness", "Grubość"},
        {"Times the physics compute work on the GPU from a background thread,\nwithout waiting for it, so the frame rate is unaffected. Figures trail\nby a frame or two. Drawing's GPU time is not included in any line.", "Mierzy obliczenia fizyki na GPU z wątku w tle,\nbez czekania, więc liczba klatek się nie zmienia. Wyniki spóźniają się\no klatkę lub dwie. Czas rysowania na GPU nie wlicza się do żadnej linii."},
        {"Tools", "Narzędzia"},
        {"Transform", "Przekształcenie"},
        {"Triangle", "Trójkąt"},
        {"Undo", "Cofnij"},
        {"Units", "Jednostki"},
        {"Units inside follow its arrow. They pass through it.", "Jednostki w środku podążają za strzałką. Przechodzą przez strefę."},
        {"Units: %u", "Jednostki: %u"},
        {"Unsaved changes", "Niezapisane zmiany"},
        {"Velocity smoothing", "Wygładzanie prędkości"},
        {"Waiting (cap, vsync)", "Czekanie (limit, vsync)"},
        {"Wall %zu", "Ściana %zu"},
        {"Wall %zu: %s##problem%zu", "Ściana %zu: %s##problem%zu"},
        {"Wall has zero or negative size", "Ściana ma zerowy lub ujemny rozmiar"},
        {"Wall lies outside the level bounds", "Ściana leży poza granicami poziomu"},
        {"Walls (last draws on top)", "Ściany (ostatnia na wierzchu)"},
        {"Warnings", "Ostrzeżenia"},
        {"Width/Height", "Szerokość/wysokość"},
        {"Wobble", "Chybotanie"},
        {"Wobble rate", "Tempo chybotania"},
        {"World units a unit at the centre is moved in all.\nNegative pulls units in.", "O ile jednostek świata przesuwa się jednostka w centrum.\nUjemna przyciąga jednostki."},
        {"east", "wschód"},
        {"fix %zu problem(s)", "do poprawy: %zu"},
        {"north", "północ"},
        {"not measured", "nie mierzono"},
        {"south", "południe"},
        {"west", "zachód"},
    };
    return table;
}

} // namespace

namespace polish {

bool enabled() {
    return g_polish;
}

void setEnabled(bool on) {
    g_polish = on;
}

} // namespace polish

const char* tr(const char* english) {
    if (!g_polish) {
        return english;
    }
    const auto& table = translations();
    const auto found = table.find(english);
    return found != table.end() ? found->second : english;
}

} // namespace horde
