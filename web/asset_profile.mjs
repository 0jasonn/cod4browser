const BASE_ARCHIVES = Object.freeze(Array.from(
    { length: 14 },
    (_, index) => `main/iw_${String(index).padStart(2, "0")}.iwd`,
));
const LOCALIZED_ARCHIVES = Object.freeze(Array.from(
    { length: 7 },
    (_, index) => `main/localized_english_iw${String(index).padStart(2, "0")}.iwd`,
));
const STARTUP_ZONES = Object.freeze([
    "zone/english/code_post_gfx.ff",
    "zone/english/ui.ff",
    "zone/english/common.ff",
]);
export const MAP_ZONE = "zone/english/killhouse.ff";

export const M12_INSTALL_PROFILE = Object.freeze({
    version: 1,
    id: "sp-killhouse-english-v1",
    product: "offline-single-player",
    language: "english",
    map: "killhouse",
    baseArchives: BASE_ARCHIVES,
    localizedArchives: LOCALIZED_ARCHIVES,
    startupZones: STARTUP_ZONES,
    mapZone: MAP_ZONE,
});

export const REQUIRED_ASSETS = Object.freeze([
    Object.freeze({
        path: "localization.txt",
        minimumSize: 1,
        maximumSize: 4095,
        label: "Localization configuration",
        kind: "localization",
    }),
    ...BASE_ARCHIVES.map((path) => Object.freeze({
        path, minimumSize: 100, maximumSize: 512 * 1024 * 1024,
        label: "Base asset archive", kind: "iwd",
    })),
    ...LOCALIZED_ARCHIVES.map((path) => Object.freeze({
        path, minimumSize: 100, maximumSize: 512 * 1024 * 1024,
        label: "English localized asset archive", kind: "iwd",
    })),
    ...STARTUP_ZONES.map((path) => Object.freeze({
        path, minimumSize: 14, maximumSize: 512 * 1024 * 1024,
        label: "Single-player startup fastfile", kind: "fastfile",
    })),
    Object.freeze({
        path: MAP_ZONE,
        minimumSize: 14,
        maximumSize: 512 * 1024 * 1024,
        label: "F.N.G. map fastfile",
        kind: "fastfile",
    }),
]);

const REQUIRED_ASSET_PATHS = new Set(REQUIRED_ASSETS.map(({ path }) => path));

export function isAdditionalSinglePlayerFastfile(
    path, language = M12_INSTALL_PROFILE.language)
{
    const prefix = `zone/${language}/`;
    if (!path.startsWith(prefix) || path.length <= prefix.length ||
        path.slice(prefix.length).includes("/") || !path.endsWith(".ff")) return false;
    const name = path.slice(prefix.length, -3);
    // Do not admit either COD4 multiplayer naming family into the offline SP profile.
    return !name.startsWith("mp_") && !name.endsWith("_mp");
}

export function isSupportedImportedPath(path)
{
    return REQUIRED_ASSET_PATHS.has(path) || isAdditionalSinglePlayerFastfile(path);
}
