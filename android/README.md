# Gradle

This folder contains a minimal Android gradle project that builds the SDL_helloworld application.
Instead of building SDL3 itself, it uses a prebuilt SDL3.

> The required `SDL3-*.aar` is no longer committed to this repo. Run
> `scripts/fetch-paks.sh --sdl` (or `scripts\fetch-paks.bat --sdl` on Windows)
> from the repo root to drop it into `app/libs/` before invoking gradle.

## FAQ

## Where do I get a prebuilt SDL3 Android library?

Prebuilt Android archives are part of SDL 3 releases as `.aar` archives.
Download these and put them in the `app/libs` folder. The `scripts/fetch-paks.*`
helper at the repo root automates this for the version this repo expects.

## I downloaded a prebuilt SDL3 library, but it cannot find `SDL3-x.y.z.aar`

In `app/build.gradle`, there is a line `implementation files('libs/SDL3-X.Y.Z.aar')`.
You must download exactly SDL3 X.Y.Z, or modify `app/build.gradle` to the version of the library you downloaded.

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
2. Add the downloaded Android archive to your dependencies:
   ```gradle
   dependencies {
     /* ... */      
     implementation files('libs/SDL3-3.2.16.aar')
   }
   ```
