export class LockManager
{
    holders = new Map();
    waiters = [];
    log = [];

    available(name, mode)
    {
        const holders = this.holders.get(name) ?? [];
        return holders.length === 0 ||
            (mode === "shared" && holders.every((holder) => holder.mode === "shared"));
    }

    held(name) { return (this.holders.get(name)?.length ?? 0) > 0; }

    request(name, options, callback)
    {
        const mode = options?.mode ?? "exclusive";
        if (options?.ifAvailable && !this.available(name, mode)) {
            return Promise.resolve(callback(null));
        }
        return new Promise((resolve, reject) => {
            const start = () => {
                if (!this.available(name, mode)) return false;
                const holder = { mode };
                const holders = this.holders.get(name) ?? [];
                holders.push(holder);
                this.holders.set(name, holders);
                this.log.push(`lock:${name}:acquire`);
                Promise.resolve(callback({ name, mode })).then(resolve, reject).finally(() => {
                    const current = this.holders.get(name) ?? [];
                    current.splice(current.indexOf(holder), 1);
                    if (current.length === 0) this.holders.delete(name);
                    this.log.push(`lock:${name}:release`);
                    this.drain();
                });
                return true;
            };
            if (!start()) this.waiters.push(start);
        });
    }

    drain()
    {
        const waiters = this.waiters;
        this.waiters = [];
        for (const start of waiters) {
            if (!start()) this.waiters.push(start);
        }
    }
}

