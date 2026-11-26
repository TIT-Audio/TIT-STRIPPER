#include "../comp/TitConfig.h"
#include <cstdio>
static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FALLO: %s\n", what); ++fails; } }
int main()
{
    using namespace titcfg;
    check (getValue ("color = off\n", "color") == "off", "leer valor");
    check (getValue ("# color = on\nCOLOR=  Off  \r\n", "color") == "Off", "mayusculas, comentario y espacios");
    check (getValue ("order = EQ\n", "color").empty(), "clave ausente");
    check (parseOnOff ("off", true) == false && parseOnOff ("ON", false) == true && parseOnOff ("0", true) == false, "on/off");
    check (parseOnOff ("", true) == true && parseOnOff ("quizas", false) == false, "ilegible -> por defecto");
    const std::string a = setValue ("", "color", "off");
    check (getValue (a, "color") == "off", "crear desde vacio");
    const std::string b = setValue (a, "order", "EQ, GATE, COMP");
    check (getValue (b, "color") == "off" && getValue (b, "order") == "EQ, GATE, COMP", "anadir clave conserva las otras");
    const std::string c = setValue (b, "color", "on");
    check (getValue (c, "color") == "on" && getValue (c, "order") == "EQ, GATE, COMP", "sustituir una clave conserva las otras");
    check (c.find ("color = off") == std::string::npos, "no deja la linea vieja");
    check (setValue ("a = 1\r\nb = 2\r\n", "a", "9") == "a = 9\nb = 2\n", "fin de linea de Windows");
    if (! fails) std::printf ("Todas las pruebas de configuracion han pasado.\n");
    return fails;
}
