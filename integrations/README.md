# Advanced Material Editor compatibility

The MMD provider adds complete material inventories, editing/saving/reset/duplication above slot 31, material visibility, posed UV geometry, and visible MMD previews. Ordinary entities retain AME's original 32-slot limit. The loader and the provider must both be installed.

The current development installation has already been updated through its existing addon symlink. Other installations can apply only this compatibility change with:

```powershell
./scripts/install-ame-compat.ps1 -AddonRoot 'C:\path\to\advanced_material_editor'
```

Git is required. The installer checks the full patch before writing anything; an already applied patch is a no-op. Conflicting edits cause it to stop without overwriting files. It does not reset the repository, replace the addon, change RTX backend settings, or stage/commit files. Restart GMod after installation.

The patch was checked against AME commit `2358fd277817947d9284ee74f314de4db86b3280` and against the current modified development tree. It intentionally excludes all unrelated RTX and documentation edits. This package distributes the compatibility diff and new provider, not a copy of the AME repository.
