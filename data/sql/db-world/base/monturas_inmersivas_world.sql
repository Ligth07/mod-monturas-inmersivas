-- AzerothCore WotLK: plantilla, modelo y aparicion de Villadorada.
-- Conserva las filas existentes. Comprueba que los IDs esten libres en instalaciones nuevas.
INSERT INTO `creature_template`
    (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`,
     `speed_walk`, `speed_run`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `type`)
SELECT 901087, 'Montura estacionada', 'Montura inmersiva', 1, 1, 35, 1, 1, 1.14286, 0, 0, 1, 770, 1
WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 901087);

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`)
SELECT 901087, 0, 2404, 1, 1
FROM `creature_template`
WHERE `entry` = 901087 AND `name` = 'Montura estacionada' AND `subname` = 'Montura inmersiva'
  AND NOT EXISTS (SELECT 1 FROM `creature_template_model` WHERE `CreatureID` = 901087 AND `Idx` = 0);

INSERT INTO `creature`
    (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
     `orientation`, `spawntimesecs`, `curhealth`, `MovementType`)
SELECT 5300684, 901087, 0, 1, 1, -9470.68, 47.6167, 56.8601, 4.55524, 300, 42, 0
FROM `creature_template`
WHERE `entry` = 901087 AND `name` = 'Montura estacionada' AND `subname` = 'Montura inmersiva'
  AND NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 5300684);
