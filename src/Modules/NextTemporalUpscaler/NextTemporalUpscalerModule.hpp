#pragma once

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextTemporalUpscaler
{
    void Install(const Runtime::Config::Options& options);
}
