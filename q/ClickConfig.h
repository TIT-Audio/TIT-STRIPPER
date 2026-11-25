#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include "Parameters.h"

namespace titq
{
// Comportamiento configurable de clics / dobles clics en la gráfica. Se guarda en archivos .json de texto plano
// en  Documentos/TIT Q/Presets/  y se carga siempre con valores seguros: si falta un campo o es inválido, se usa el de fábrica.
struct ClickConfig
{
    // --- banda nueva ---
    juce::String newBandType = "Bell";          // cualquiera de los nombres de curva (Bell, Low Shelf, BOOBTEC, AP TIT...)
    double newBandSlope = 12.0;                 // dB/oct (cortes y shelves)
    double newBandQ = 1.0;
    bool   gainFromClick = false;               // true: la banda nueva nace con la ganancia de la altura del clic

    // --- zonas de frecuencia: un clic (o doble clic) en una zona vacia crea la banda que se indique ---
    struct Zone
    {
        juce::String action;            // HighPass | LowPass | LowShelf | HighShelf | Bell | None
        double minHz = 20.0, maxHz = 30.0, slope = 12.0;
        double minDb = -24.0, maxDb = 24.0;   // altura del clic (dB) en la que vale la zona
        int clicks = 1;                 // 1 = un clic, 2 = doble clic
    };
    enum { kBass, kLowShelf, kHighShelf, kTreble, kNumZones };
    Zone zones[kNumZones] = { { "HighPass",  20.0,    30.0,    12.0, -24.0, 24.0, 1 },
                              { "LowShelf",  31.0,    50.0,    12.0, -24.0, 24.0, 1 },
                              { "HighShelf", 12000.0, 18000.0, 12.0, -24.0, 24.0, 2 },
                              { "LowPass",   19000.0, 21000.0, 12.0, -24.0, 24.0, 1 } };
    static const char* zoneKey (int i)  { static const char* k[] = { "bassZone", "lowShelfZone", "highShelfZone", "trebleZone" }; return k[i]; }
    static const char* zoneName (int i) { static const char* k[] = { "Bass zone", "Low shelf zone", "High shelf zone", "Treble zone" }; return k[i]; }

    // --- rueda del raton sobre una banda: cambia la Q ---
    bool   wheelQ = true;
    double wheelQSpeed = 1.0;           // 0.2 .. 4

    // --- doble clic sobre una banda existente ---
    juce::String bandDoubleClick = "ToggleOnOff";   // ToggleOnOff | Delete | None

    static juce::StringArray zoneActions()       { return { "HighPass", "LowPass", "LowShelf", "HighShelf", "Bell", "None" }; }
    static juce::StringArray bandClickActions()  { return { "ToggleOnOff", "Delete", "None" }; }

    // nombre de curva (para el parámetro "type") que corresponde a una acción de zona
    static juce::String typeForAction (const juce::String& a)
    {
        if (a == "HighPass")  return "Low Cut";     // High Pass = corta graves
        if (a == "LowPass")   return "High Cut";    // Low Pass = corta agudos
        if (a == "LowShelf")  return "Low Shelf";
        if (a == "HighShelf") return "High Shelf";
        if (a == "Bell")      return "Bell";
        return {};
    }

    // ---------- JSON ----------
    juce::var toVar() const
    {
        auto* nb = new juce::DynamicObject();
        nb->setProperty ("type", newBandType);
        nb->setProperty ("slope", newBandSlope);
        nb->setProperty ("q", newBandQ);
        nb->setProperty ("gainFromClick", gainFromClick);

        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 2);
        root->setProperty ("newBand", juce::var (nb));
        for (int i = 0; i < kNumZones; ++i)
        {
            auto* z = new juce::DynamicObject();
            z->setProperty ("action", zones[i].action);
            z->setProperty ("minHz", zones[i].minHz);
            z->setProperty ("maxHz", zones[i].maxHz);
            z->setProperty ("slope", zones[i].slope);
            z->setProperty ("minDb", zones[i].minDb);
            z->setProperty ("maxDb", zones[i].maxDb);
            z->setProperty ("clicks", zones[i].clicks);
            root->setProperty (zoneKey (i), juce::var (z));
        }
        root->setProperty ("wheelQ", wheelQ);
        root->setProperty ("wheelQSpeed", wheelQSpeed);
        root->setProperty ("doubleClickOnBand", bandDoubleClick);
        return juce::var (root);
    }

    static ClickConfig fromVar (const juce::var& v)
    {
        ClickConfig c;                                  // valores de fábrica
        if (! v.isObject()) return c;

        const auto nb = v.getProperty ("newBand", juce::var());
        c.newBandType     = pickEnum (nb.getProperty ("type", juce::var()), typeNames(), c.newBandType);
        c.newBandSlope    = num (nb.getProperty ("slope", juce::var()), 0.0, 96.0, c.newBandSlope);
        c.newBandQ        = num (nb.getProperty ("q", juce::var()), 0.025, 40.0, c.newBandQ);
        c.gainFromClick   = flag (nb.getProperty ("gainFromClick", juce::var()), c.gainFromClick);

        const ClickConfig defaults;
        for (int i = 0; i < kNumZones; ++i)
        {
            const auto z = v.getProperty (zoneKey (i), juce::var());
            auto& d = c.zones[i];
            d.action = pickEnum (z.getProperty ("action", juce::var()), zoneActions(), d.action);
            d.minHz  = num (z.getProperty ("minHz", juce::var()), 10.0, 21000.0, d.minHz);
            d.maxHz  = num (z.getProperty ("maxHz", juce::var()), 10.0, 21000.0, d.maxHz);
            d.slope  = num (z.getProperty ("slope", juce::var()), 0.0, 96.0, d.slope);
            d.minDb  = num (z.getProperty ("minDb", juce::var()), -60.0, 60.0, d.minDb);
            d.maxDb  = num (z.getProperty ("maxDb", juce::var()), -60.0, 60.0, d.maxDb);
            d.clicks = (int) num (z.getProperty ("clicks", juce::var()), 1.0, 2.0, (double) d.clicks);
            if (d.minHz >= d.maxHz || d.minDb >= d.maxDb) d = defaults.zones[i];     // coherencia de los rangos de frecuencia
        }
        c.wheelQ      = flag (v.getProperty ("wheelQ", juce::var()), c.wheelQ);
        c.wheelQSpeed = num (v.getProperty ("wheelQSpeed", juce::var()), 0.2, 4.0, c.wheelQSpeed);

        c.bandDoubleClick = pickEnum (v.getProperty ("doubleClickOnBand", juce::var()), bandClickActions(), c.bandDoubleClick);

        return c;
    }

    // ---------- carpeta y archivos ----------
    static juce::File presetsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("TIT Q").getChildFile ("Presets");
    }

    static juce::Array<juce::File> listPresets()
    {
        juce::Array<juce::File> out;
        for (auto& f : presetsFolder().findChildFiles (juce::File::findFiles, false, "*.json"))
            if (! f.getFileName().startsWithChar ('_'))      // los archivos que empiezan por "_" son internos (_default.json)
                out.add (f);
        std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });
        return out;
    }

    static juce::File fileFor (const juce::String& name)
    {
        return presetsFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".json");
    }

    static ClickConfig load (const juce::File& f)
    {
        if (! f.existsAsFile()) return {};
        return fromVar (juce::JSON::parse (f));              // JSON roto o incompleto -> valores de fábrica en lo que falte
    }

    static bool save (const juce::File& f, const ClickConfig& c)
    {
        f.getParentDirectory().createDirectory();
        return f.replaceWithText (juce::JSON::toString (c.toVar()));
    }

    // el preset marcado como Default se recuerda en _default.json
    static juce::String defaultName()
    {
        const auto v = juce::JSON::parse (presetsFolder().getChildFile ("_default.json"));
        return v.getProperty ("default", juce::var()).toString();
    }

    static void setDefaultName (const juce::String& name)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("default", name);
        presetsFolder().getChildFile ("_default.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    // Arranque: carga el preset Default. Si no hay carpeta/archivos, crea Default.json con los valores de fábrica.
    // Nunca falla: ante cualquier problema devuelve los valores de fábrica.
    static ClickConfig loadDefault (juce::String& nameOut)
    {
        nameOut = "Default";
        const auto folder = presetsFolder();
        folder.createDirectory();

        if (listPresets().isEmpty())
        {
            save (folder.getChildFile ("Default.json"), ClickConfig());
            setDefaultName ("Default");
        }

        juce::String name = defaultName();
        if (name.isEmpty() || ! fileFor (name).existsAsFile())
        {
            const auto all = listPresets();
            if (all.isEmpty()) return {};
            name = all.getFirst().getFileNameWithoutExtension();
        }
        nameOut = name;
        return load (fileFor (name));
    }

private:
    static juce::String pickEnum (const juce::var& v, const juce::StringArray& allowed, const juce::String& def)
    {
        const auto s = v.toString().trim();
        for (auto& a : allowed) if (a.equalsIgnoreCase (s)) return a;
        return def;
    }
    static double num (const juce::var& v, double lo, double hi, double def)
    {
        if (! (v.isDouble() || v.isInt() || v.isInt64())) return def;
        const double d = (double) v;
        return std::isfinite (d) ? juce::jlimit (lo, hi, d) : def;
    }
    static bool flag (const juce::var& v, bool def)
    {
        return (v.isBool() || v.isInt() || v.isInt64()) ? (bool) v : def;
    }
};
} // namespace titq
