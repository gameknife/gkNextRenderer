# Gradle

This folder contains the Android gradle project that builds gkNextEngine for Android
(`applicationId com.gknext.renderer`, based on SDL's helloworld activity template).
Instead of building SDL3 itself, it uses a prebuilt SDL3.

> The required `SDL3-*.aar` is **not** committed to this repo and is no longer
> mirrored on our paks release. The `downloadSdlAar` task in
> `app/build.gradle` pulls `SDL3-devel-<ver>-android.zip` directly from
> [libsdl-org's official release](https://github.com/libsdl-org/SDL/releases)
> on first build and extracts the `.aar` into `app/libs/`. No manual fetch
> step is required.

## FAQ

## Where do I get a prebuilt SDL3 Android library?

Prebuilt Android archives are part of SDL 3 releases as `.aar` archives,
published inside `SDL3-devel-X.Y.Z-android.zip`. The `downloadSdlAar` Gradle
task handles this automatically; bump `ext.sdlVersion` in `app/build.gradle`
to track a different SDL3 release.

## Gradle cannot find `SDL3-x.y.z.aar`

`app/build.gradle` derives both the download URL and dependency path from
`ext.sdlVersion`. If the build reports a missing `SDL3-x.y.z.aar`, make sure
`ext.sdlVersion` points to a SDL release that publishes
`SDL3-devel-x.y.z-android.zip`, then rerun Gradle so `downloadSdlAar` can
hydrate `app/libs/`.

## How do I modify my Android project to use prebuilt SDL3 Android archives?

Only 2 changes are required:

1. Enable the prefab build feature:
   ```gradle
   android {
       /* ... */
       buildFeatures {
           prefab true
       }
   }
   ```
2. Add the generated Android archive path to your dependencies:
   ```gradle
   ext.sdlVersion = '3.2.22'

   dependencies {
     /* ... */      
     implementation files("libs/SDL3-${sdlVersion}.aar")
   }
   ```
