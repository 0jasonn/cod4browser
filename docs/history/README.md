# Historical web-port records

Superseded milestone snapshots and root completion reports live in Git.
The last revision containing the complete set is
`49d6168cab15181f03744cf07f10b288b673bc0c`.

List or read an archived document without changing the working tree:

```powershell
git ls-tree -r --name-only 49d6168c docs/history
git show 49d6168c:docs/history/web-port-milestones.md
git show 49d6168c:WEB_ROADMAP_EXECUTION_REPORT.md
git log --all -- docs/history
```

Use [current status](../web-status.md), [the roadmap](../web-roadmap.md),
[system ownership](../web-port-convergence.md), and the retained
[evidence records](../evidence/) for ongoing work.
