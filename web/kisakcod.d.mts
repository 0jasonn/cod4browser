export interface KisakModule {
    HEAPU8: Uint8Array;
    _malloc(bytes: number): number;
    _free(pointer: number): void;
    _KisakWeb_MountCanonicalRuntime(): number;
    _KisakWeb_ProbeLocalization(pointer: number, length: number, size: number): number;
    _KisakWeb_ProbeFastfileHeader(pointer: number, length: number, size: number): number;
    _KisakWeb_ProbeIwd(head: number, headLength: number, tail: number, tailLength: number,
        tailOffset: number, central: number, centralLength: number, centralOffset: number, size: number): number;
    _KisakWeb_SubmitCanonicalCommand(pointer: number): number;
    _KisakWeb_QueueKeyEvent(key: number, down: number): void;
    _KisakWeb_QueueCharEvent(character: number): void;
    _KisakWeb_SetClipboardText(pointer: number, length: number): number;
    _KisakWeb_QueueMouseMove(x: number, y: number, dx: number, dy: number): void;
    browserCanvasSize?: [number, number];
    webImageClosing?: boolean;
    webImageTasks?: Set<Promise<unknown>>;
}
declare const createKisakCOD: (options: Record<string, unknown>) => Promise<KisakModule>;
export default createKisakCOD;
