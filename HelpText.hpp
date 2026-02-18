#ifndef HELPTEXT_HPP
#define HELPTEXT_HPP

#include <QString>

namespace Radio
{
    class HelpText
    {
    public:
        static QString keyboardShortcuts();
        static QString specialMouseCommands();
        static QString prefixes();
    };
}

#endif // HELPTEXT_HPP
