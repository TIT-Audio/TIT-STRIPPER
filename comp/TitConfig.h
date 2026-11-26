#pragma once
// Archivo de configuracion de los plugins TIT (texto plano "clave = valor", lineas con # son comentarios).
// Sin dependencias de JUCE para poder probarlo en los tests del nucleo. Lo comparten TIT COMP y TIT STRIPPER.
#include <algorithm>
#include <cctype>
#include <string>

namespace titcfg
{
    inline std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace ((unsigned char) s[a])) ++a;
        while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
        return s.substr (a, b - a);
    }

    inline std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }

    inline std::string header()
    {
        return "# Configuracion de los plugins TIT (se puede editar a mano o desde el menu \"...\" del plugin).\n";
    }

    // Valor de una clave (sin distinguir mayusculas), o "" si no esta
    inline std::string getValue (const std::string& text, const std::string& key)
    {
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t end = text.find ('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = trim (text.substr (pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find ('=');
            if (eq != std::string::npos && upper (trim (line.substr (0, eq))) == upper (key))
                return trim (line.substr (eq + 1));
        }
        return {};
    }

    // Devuelve el texto con la clave puesta a value (sustituye la linea o la anade al final); conserva el resto
    inline std::string setValue (std::string text, const std::string& key, const std::string& value)
    {
        if (text.empty()) text = header();
        std::string out;
        bool done = false;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find ('\n', pos);
            const bool last = (end == std::string::npos);
            if (last) end = text.size();
            std::string line = text.substr (pos, end - pos);
            pos = end + 1;
            if (! line.empty() && line.back() == '\r') line.pop_back();
            const std::string t = trim (line);
            const size_t eq = t.find ('=');
            if (! done && ! t.empty() && t[0] != '#' && eq != std::string::npos && upper (trim (t.substr (0, eq))) == upper (key))
            {
                out += key + " = " + value + "\n";
                done = true;
            }
            else if (! (last && line.empty()))
                out += line + "\n";
        }
        if (! done) out += key + " = " + value + "\n";
        return out;
    }

    // on / off / yes / no / true / false / 1 / 0; si no se entiende, devuelve def
    inline bool parseOnOff (const std::string& value, bool def)
    {
        const std::string v = upper (trim (value));
        if (v == "ON" || v == "YES" || v == "TRUE" || v == "1" || v == "SI") return true;
        if (v == "OFF" || v == "NO" || v == "FALSE" || v == "0") return false;
        return def;
    }
}
