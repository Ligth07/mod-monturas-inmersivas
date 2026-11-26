# Monturas Inmersivas

Módulo C++ para AzerothCore WotLK 3.3.5a y addon para el cliente. Permite pedir un caballo prestado, dejarlo estacionado, volver a montarlo, alimentarlo y llevarlo con una correa. El nivel sigue al jinete y la felicidad influye en la velocidad. La lógica del servidor funciona sin Eluna ni AIO.

## Instalación

1. Coloca el repositorio en `modules/mod-monturas-inmersivas`.
2. En la base de datos de mundo, prepara el NPC interactivo con entrada `901087` y bandera de diálogo. Configura `MonturasInmersivas.StableSpawnId` con el GUID de su aparición en tu establo. El valor de ejemplo `5300684` corresponde a la instalación original.
3. Si aún falta la tabla de felicidad, ejecuta en la base de datos de personajes:

   ~~~sql
   CREATE TABLE IF NOT EXISTS immersive_mount_happiness (
       guid INT UNSIGNED NOT NULL,
       spell INT UNSIGNED NOT NULL,
       happiness TINYINT UNSIGNED NOT NULL,
       PRIMARY KEY (guid, spell)
   ) ENGINE=InnoDB;
   ~~~

4. Regenera CMake y compila `worldserver`. El proyecto necesita los encabezados de MySQL que utiliza AzerothCore y una revisión del core compatible con `ModuleDatabasePool`.
5. Copia la configuración de ejemplo a `configs/modules/monturas_inmersivas.conf`, ajusta el GUID y pon `MonturasInmersivas.Enable = 1`. Si vienes de la versión Lua, quita sus dos scripts de `lua_scripts` antes de activar el módulo.
6. Copia `client/MonturasInmersivas` a `Interface/AddOns` en el cliente de cada jugador. Abre WoW y habilita **Monturas Inmersivas**.

Haz clic derecho en el caballo del establo para pedirlo. Tras desmontar, selecciona tu caballo para ver la felicidad y usar los iconos de comida y correa. El comando GM `.montura hambre` permite probar el desgaste de felicidad.

La configuración de ejemplo controla la reaparición del caballo, el tiempo de abandono y la distancia de seguimiento. El módulo usa efectos existentes de 3.3.5a.
