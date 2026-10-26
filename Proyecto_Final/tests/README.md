# Pruebas por bloque

Cada autor incorpora aqui sus ensayos, con sus resultados e instrucciones.
Los formateadores y parsers puros se pueden probar sin sensores reales.
Los ensayos en placa usan CMake, configuracion y main propios y enlazan las
fuentes reales del bloque. Los dobles de prueba se limitan a esta carpeta.

La configuracion de produccion y src/build_checks.c mantienen sus restricciones;
una consola temporal de ensayo debe declararse en su propia configuracion.
Las compilaciones generadas se guardan en build o build-* y se excluyen de Git.

D1 compila el main minimo y todos los headers publicos. Las validaciones de
sensores, GPS, formatos y aplicacion se agregan en D2-D4 y P1-P4.
