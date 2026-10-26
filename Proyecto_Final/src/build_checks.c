#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(
    !IS_ENABLED(CONFIG_CONSOLE) &&
    !IS_ENABLED(CONFIG_UART_CONSOLE) &&
    !IS_ENABLED(CONFIG_STDOUT_CONSOLE) &&
    !IS_ENABLED(CONFIG_PRINTK) &&
    !IS_ENABLED(CONFIG_BOOT_BANNER) &&
    !IS_ENABLED(CONFIG_SHELL) &&
    !IS_ENABLED(CONFIG_LOG),
    "Produccion requiere consola, printk, banner, shell y logs desactivados"
);

BUILD_ASSERT(
    !DT_HAS_CHOSEN(zephyr_console) &&
    !DT_HAS_CHOSEN(zephyr_shell_uart),
    "Produccion requiere retirar las asignaciones de consola del Devicetree"
);
