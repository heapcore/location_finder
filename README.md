# location_finder

> **WARNING:** This repository may be unstable or non-functional. Use at your own risk.

Qt Widgets app for loading a CSV file and finding points near a target `(x y z)` coordinate.
It is intended for searching coordinates in map dumps exported from Unreal Engine projects.

## What It Does

- Opens a CSV file and shows a preview in a table.
- Accepts a query in the form `x y z`.
- Filters rows where coordinates are inside `query +/- lag` for each axis.
- Displays up to 100 matching rows.

## Supported CSV Formats

Use one of these formats:

1. Separate coordinate columns:
   `...,X,Y,Z,...`
2. Single combined coordinate column:
   `...,RelativeLocation,...` where value is `x y z`

`data.csv` contains a working example using `X,Y,Z`.

## Unreal Engine Dump Format Example

Recommended CSV structure for map dump exports:

```csv
Name,Category,X,Y,Z,Timestamp
SM_Crate_01,StaticMeshActor,120.0,80.0,30.0,1678886400
BP_Enemy_Grunt_C_12,BlueprintGeneratedClass,150.5,90.2,40.7,1678886460
```

Alternative compatible format (single coordinate field):

```csv
Name,Category,RelativeLocation,Timestamp
SM_Crate_01,StaticMeshActor,"120.0 80.0 30.0",1678886400
```

## Build and Run

1. Configure:
   `cmake -S . -B build`
2. Build:
   `cmake --build build`
3. Run:
   `build/location_finder`

## License

See `LICENSE`.
