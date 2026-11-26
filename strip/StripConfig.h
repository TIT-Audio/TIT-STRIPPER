#pragma once
// Archivo de configuracion de TIT STRIPPER (texto plano): orden con el que arranca una instancia nueva.
// Sin dependencias de JUCE para poder probarlo en los tests del nucleo.
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include "../comp/TitConfig.h"

namespace stripcfg
{
    // Permutaciones en el mismo orden que StripProcessor::orders(); modulos: 0 = GATE, 1 = COMP, 2 = EQ
    inline const std::array<std::array<int, 3>, 6>& permutations()
    {
        static const std::array<std::array<int, 3>, 6> o { { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
        return o;
    }

        inline std::string defaultText()
    {
        return "# TIT STRIPPER - configuracion (se puede editar a mano o desde el menu \"...\" del plugin)\n"
               "# order: orden con el que arranca una instancia nueva (los proyectos guardados conservan el suyo).\n"
               "#        Escribe los tres modulos separados por comas, de izquierda a derecha: GATE, COMP y EQ.\n"
               "#        Ejemplos:  order = EQ, GATE, COMP     order = GATE, EQ, COMP\n"
               "# color: estado del boton Color del compresor en una instancia nueva (on / off).\n"
               "order = GATE, COMP, EQ\n"
               "color = on\n";
    }

    inline std::string orderValue (int idx)
    {
        static const char* n[] = { "GATE", "COMP", "EQ" };
        idx = std::max (0, std::min (5, idx));
        std::string v;
        for (int k = 0; k < 3; ++k) v += std::string (k ? ", " : "") + n[permutations()[(size_t) idx][(size_t) k]];
        return v;
    }

    // Texto con el orden idx (0..5) puesto en "text" (o en el texto por defecto si esta vacio); conserva el resto de claves
    inline std::string textForOrder (int idx, const std::string& text = std::string())
    {
        return titcfg::setValue (text.empty() ? defaultText() : text, "order", orderValue (idx));
    }

    // Indice (0..5) de la permutacion pedida en el texto, o 0 si falta o es invalida
    inline int parseOrderIndex (const std::string& text)
    {
        std::string v = titcfg::upper (titcfg::getValue (text, "order"));
        if (v.empty()) return 0;
        for (auto& c : v) if (c == '>' || c == ';' || c == '/') c = ',';
        std::array<int, 3> o { -1, -1, -1 };
        int n = 0;
        size_t s = 0;
        while (s <= v.size())
        {
            size_t e = v.find (',', s);
            if (e == std::string::npos) e = v.size();
            const std::string tok = titcfg::trim (v.substr (s, e - s));
            s = e + 1;
            if (tok.empty()) continue;
            const int m = tok == "GATE" ? 0 : tok == "COMP" ? 1 : tok == "EQ" ? 2 : -1;
            if (m < 0 || n >= 3) return 0;
            o[(size_t) n++] = m;
        }
        for (size_t i = 0; i < permutations().size(); ++i)
            if (permutations()[i] == o) return (int) i;
        return 0;
    }
}
