#include "CountryNames.hpp"

namespace Radio
{
    QString CountryNames::abbreviate(QString name)
    {
        name.replace ("Islands", "Is.");
        name.replace ("Island", "Is.");
        name.replace ("North ", "N. ");
        name.replace ("Northern ", "N. ");
        name.replace ("South ", "S. ");
        name.replace ("East ", "E. ");
        name.replace ("Eastern ", "E. ");
        name.replace ("West ", "W. ");
        name.replace ("Western ", "W. ");
        name.replace ("Central ", "C. ");
        name.replace (" and ", " & ");
        name.replace ("Republic", "Rep.");
        name.replace ("United States of America", "U.S.A.");
        name.replace ("United States", "U.S.A.");
        name.replace ("Fed. Rep. of ", "");
        name.replace ("French ", "Fr.");
        name.replace ("Asiatic", "AS");
        name.replace ("European", "EU");
        name.replace ("African", "AF");
        return name;
    }
}
