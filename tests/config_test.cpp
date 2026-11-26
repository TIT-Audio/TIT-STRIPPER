#include "../strip/StripConfig.h"
#include <cstdio>
static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FALLO: %s\n", what); ++fails; } }
int main()
{
    using namespace stripcfg;
    check (parseOrderIndex (defaultText()) == 0, "texto por defecto = GATE, COMP, EQ");
    check (parseOrderIndex ("order = EQ, GATE, COMP") == 4, "EQ, GATE, COMP");
    check (parseOrderIndex ("# nota\n  Order=comp > eq > gate  \n") == 3, "minusculas, > y espacios");
    check (parseOrderIndex ("order = GATE, EQ, COMP\r\n") == 1, "fin de linea de Windows");
    check (parseOrderIndex ("order = EQ, COMP, GATE") == 5, "EQ, COMP, GATE");
    check (parseOrderIndex ("order = COMP, GATE, EQ") == 2, "COMP, GATE, EQ");
    check (parseOrderIndex ("order = EQ, EQ, GATE") == 0, "repetido -> por defecto");
    check (parseOrderIndex ("order = EQ, GATE") == 0, "incompleto -> por defecto");
    check (parseOrderIndex ("order = EQ, GATE, COMP, X") == 0, "demasiados -> por defecto");
    check (parseOrderIndex ("order = foo") == 0, "modulo desconocido -> por defecto");
    check (parseOrderIndex ("") == 0, "vacio -> por defecto");
    for (size_t i = 0; i < permutations().size(); ++i)
    {
        const char* n[] = { "GATE", "COMP", "EQ" };
        std::string s = "order = "; for (int k = 0; k < 3; ++k) s += std::string (k ? ", " : "") + n[permutations()[i][(size_t) k]];
        check (parseOrderIndex (s) == (int) i, "las 6 permutaciones");
    }
    for (int i = 0; i < 6; ++i) check (parseOrderIndex (textForOrder (i)) == i, "texto generado -> mismo orden");
    check (titcfg::getValue (textForOrder (3, "color = off\n"), "color") == "off", "cambiar el orden conserva color");
    check (titcfg::getValue (defaultText(), "color") == "on", "color por defecto on");
    check (textForOrder (4).find ("order = EQ, GATE, COMP") != std::string::npos, "texto con EQ primero");
    if (! fails) std::printf ("Todas las pruebas de configuracion han pasado.\n");
    return fails;
}
