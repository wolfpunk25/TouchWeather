#include "daisy_seed.h"

#include "app.h"

static tw::Application app;

extern "C" int main(void)
{
    app.Init();
    app.Loop();
}
