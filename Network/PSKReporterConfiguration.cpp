#include "PSKReporter.hpp"

#include <QDir>

#include "Configuration.hpp"

namespace
{
  PSKReporter::Options options_from_config (Configuration const * config, QString const& program_info)
  {
    return {config->psk_reporter_tcpip (), config->data_dir ().absoluteFilePath ("eclipse.txt"), program_info};
  }
}

PSKReporter::PSKReporter (Configuration const * config, QString const& program_info)
  : PSKReporter {options_from_config (config, program_info)}
{
}
