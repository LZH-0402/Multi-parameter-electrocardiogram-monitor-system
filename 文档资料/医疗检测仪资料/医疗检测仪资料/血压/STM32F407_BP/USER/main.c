#include "stm32f4xx.h"
#include "app_bp.h"


int main(void)
{
  app_init();

  while (1)
  {
    app_loop();
  }
}
