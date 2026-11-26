#pragma once
// Archivo de configuracion de TIT STRIPPER (texto plano): orden con el que arranca una instancia nueva.
// Sin dependencias de JUCE para poder probarlo en los tests del nucleo.
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

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
        return "# TIT STRIPPER - configuracion\n"
               "# Orden con el que arranca una instancia nueva del plugin (los proyectos guardados conservan el suyo).\n"
               "# Escribe los tres modulos separados por comas, de izquierda a derecha: GATE, COMP y EQ.\n"
               "# Ejemplos:  order = EQ, GATE, COMP     order = GATE, EQ, COMP\n"
               "order = GATE, COMP, EQ\n";
    }

    // Texto del archivo con el orden idx (0..5): el mismo de defaultText() cambiando la linea order
    inline std::string textForOrder (int idx)
    {
        static const char* n[] = { "GATE", "COMP", "EQ" };
        idx = std::max (0, std::min (5, idx));
        std::string t = defaultText();
        const size_t a = t.rfind ("order = ");
        t.erase (a);
        t += "order = ";
        for (int k = 0; k < 3; ++k) t += std::string (k ? ", " : "") + n[permutations()[(size_t) idx][(size_t) k]];
        return t + "\n";
    }

    // Devuelve el indice (0..5) de la permutacion pedida en el texto, o 0 si falta o es invalida.
    inline int parseOrderIndex (const std::string& text)
    {
        auto upper = [] (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; };
        auto trim = [] (const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace ((unsigned char) s[a])) ++a;
            while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
            return s.substr (a, b - a);
        };

        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t end = text.find ('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = trim (text.substr (pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find ('=');
            if (eq == std::string::npos || upper (trim (line.substr (0, eq))) != "ORDER") continue;

            std::string v = upper (line.substr (eq + 1));
            for (auto& c : v) if (c == '>' || c == ';' || c == '/') c = ',';
            std::array<int, 3> o { -1, -1, -1 };
            int n = 0;
            size_t s = 0;
            while (s <= v.size())
            {
                size_t e = v.find (',', s);
                if (e == std::string::npos) e = v.size();
                const std::string tok = trim (v.substr (s, e - s));
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
        return 0;
    }
}
