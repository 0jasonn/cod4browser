const BASE_ARCHIVES = Object.freeze(Array.from(
    { length: 14 },
    (_, index) => `main/iw_${String(index).padStart(2, "0")}.iwd`,
));
// The existing Wasm localization probe decides which language names the
// engine supports. This platform profile only constructs safe file paths.
export function getInstallProfile(language = "english")
{
    if (typeof language !== "string" || !/^[a-z]{1,16}$/.test(language))
        throw new TypeError("Invalid installation language path component.");
    return Object.freeze({
        version: 1,
        id: `sp-killhouse-${language}-v1`,
        product: "offline-single-player",
        language,
        map: "killhouse",
        baseArchives: BASE_ARCHIVES,
        localizedArchives: Object.freeze(Array.from({ length: 7 },
            (_, index) => `main/localized_${language}_iw${String(index).padStart(2, "0")}.iwd`)),
        startupZones: Object.freeze(["code_post_gfx", "ui", "common"].map(
            (name) => `zone/${language}/${name}.ff`)),
        mapZone: `zone/${language}/killhouse.ff`,
    });
}

export function getRequiredAssets(language = "english")
{
    const profile = getInstallProfile(language);
    return Object.freeze([
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
    ...profile.localizedArchives.map((path) => Object.freeze({
        path, minimumSize: 100, maximumSize: 512 * 1024 * 1024,
        label: "Localized asset archive", kind: "iwd",
    })),
    ...profile.startupZones.map((path) => Object.freeze({
        path, minimumSize: 14, maximumSize: 512 * 1024 * 1024,
        label: "Single-player startup fastfile", kind: "fastfile",
    })),
    Object.freeze({
        path: profile.mapZone,
        minimumSize: 14,
        maximumSize: 512 * 1024 * 1024,
        label: "F.N.G. map fastfile",
        kind: "fastfile",
    }),
    ]);
}

// Retain the existing English fixture/profile exports and stored profile IDs.
export const M12_INSTALL_PROFILE = getInstallProfile();
export const MAP_ZONE = M12_INSTALL_PROFILE.mapZone;
export const REQUIRED_ASSETS = getRequiredAssets();

/** @param {string} path */
export function isCinematicPath(path)
{
    return /^main\/video\/[a-z0-9_]+\.bik$/.test(path);
}

export function isAdditionalSinglePlayerFastfile(
    /** @type {string} */
    path, language = M12_INSTALL_PROFILE.language)
{
    const prefix = `zone/${language}/`;
    if (!path.startsWith(prefix) || path.length <= prefix.length ||
        path.slice(prefix.length).includes("/") || !path.endsWith(".ff")) return false;
    const name = path.slice(prefix.length, -3);
    // Do not admit either COD4 multiplayer naming family into the offline SP profile.
    return !name.startsWith("mp_") && !name.endsWith("_mp");
}

/** @param {string} path */
export function isSupportedImportedPath(path, language = "english")
{
    return path === "localization.txt" || BASE_ARCHIVES.includes(path) ||
        Array.from({ length: 7 }, (_, index) =>
            `main/localized_${language}_iw${String(index).padStart(2, "0")}.iwd`).includes(path) ||
        isAdditionalSinglePlayerFastfile(path, language) || isCinematicPath(path);
}
