package com.gknext.renderer;

import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class GkNextActivity extends SDLActivity {

    private static final String TAG = "gknext";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enableImmersiveFullscreen();
        copyAssetsToExternalStorage();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enableImmersiveFullscreen();
        }
    }

    private void enableImmersiveFullscreen() {
        Window window = getWindow();
        View decorView = window.getDecorView();
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            WindowInsetsController controller = decorView.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private void copyAssetsToExternalStorage() {
        File destDir = getExternalFilesDir(null);
        if (destDir == null) {
            Log.e(TAG, "Failed to get external files directory.");
            return;
        }

        if (!destDir.exists()) {
            if (!destDir.mkdirs()) {
                Log.e(TAG, "Failed to create destination directory: " + destDir.getAbsolutePath());
                return;
            }
        }

        Log.d(TAG, "Starting to copy assets to " + destDir.getAbsolutePath());
        copyAssetsRecursive("", destDir);
        Log.d(TAG, "Finished copying assets.");
    }

    private void copyAssetsRecursive(String assetPath, File destDir) {
        AssetManager assetManager = getAssets();
        String[] assets;
        try {
            assets = assetManager.list(assetPath);
            if (assets == null || assets.length == 0) {
                return;
            }

            for (String asset : assets) {
                String newAssetPath = assetPath.isEmpty() ? asset : assetPath + "/" + asset;
                File destFile = new File(destDir, asset);

                if (assetManager.list(newAssetPath).length > 0) {
                    Log.d(TAG, "Creating directory: " + destFile.getAbsolutePath());
                    destFile.mkdirs();
                    copyAssetsRecursive(newAssetPath, destFile);
                } else {
                    Log.d(TAG, "Copying file: " + newAssetPath + " to " + destFile.getAbsolutePath());
                    try (InputStream in = assetManager.open(newAssetPath);
                         OutputStream out = new FileOutputStream(destFile)) {
                        copyFileStream(in, out);
                    } catch (IOException e) {
                        Log.e(TAG, "Failed to copy asset file: " + newAssetPath, e);
                    }
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "Failed to list assets for path: " + assetPath, e);
        }
    }

    private void copyFileStream(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[4096];
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
    }

    /**
     * Forward launch-time diagnostic switches to SDL's native argv. The
     * RenderDoc layer is installed and selected by the desktop replay host;
     * this flag only opts the native application into its application API and
     * automatic first-frame capture.
     */
    @Override
    protected String[] getArguments() {
        if (getIntent() != null && getIntent().getBooleanExtra("gknext.renderdoc", false)) {
            return new String[] { "--renderdoc" };
        }
        return new String[0];
    }

    /**
     * The application library this APK was built around. Every application shares this activity,
     * so the name is a build config field rather than a constant here.
     */
    @Override
    protected String[] getLibraries() {
        String nativeLibrary = "debug".equalsIgnoreCase(BuildConfig.BUILD_TYPE)
            ? BuildConfig.GK_NATIVE_LIBRARY + "d"
            : BuildConfig.GK_NATIVE_LIBRARY;
        return new String[] { "SDL3", nativeLibrary };
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint)
    {
        int orientation_landscape = -1;
        int orientation_portrait = -1;

        /* If set, hint "explicitly controls which UI orientations are allowed". */
        if (hint.contains("LandscapeRight") && hint.contains("LandscapeLeft")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE;
        } else if (hint.contains("LandscapeLeft")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        } else if (hint.contains("LandscapeRight")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;
        }

        /* exact match to 'Portrait' to distinguish with PortraitUpsideDown */
        boolean contains_Portrait = hint.contains("Portrait ") || hint.endsWith("Portrait");

        if (contains_Portrait && hint.contains("PortraitUpsideDown")) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT;
        } else if (contains_Portrait) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        } else if (hint.contains("PortraitUpsideDown")) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
        }

        boolean is_landscape_allowed = (orientation_landscape != -1);
        boolean is_portrait_allowed = (orientation_portrait != -1);
        int req; /* Requested orientation */

        /* No valid hint, nothing is explicitly allowed */
        if (!is_portrait_allowed && !is_landscape_allowed) {
            if (resizable) {
                /* All orientations are allowed, respecting user orientation lock setting */
                req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
            } else {
                /* Fixed window and nothing specified. Get orientation from w/h of created window */
                req = (w > h ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
            }
        } else {
            /* At least one orientation is allowed */
            if (resizable) {
                if (is_portrait_allowed && is_landscape_allowed) {
                    /* hint allows both landscape and portrait, promote to full user */
                    req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
                } else {
                    /* Use the only one allowed "orientation" */
                    req = (is_landscape_allowed ? orientation_landscape : orientation_portrait);
                }
            } else {
                /* Fixed window and both orientations are allowed. Choose one. */
                if (is_portrait_allowed && is_landscape_allowed) {
                    req = (w > h ? orientation_landscape : orientation_portrait);
                } else {
                    /* Use the only one allowed "orientation" */
                    req = (is_landscape_allowed ? orientation_landscape : orientation_portrait);
                }
            }
        }

        Log.v(TAG, "setOrientation() requestedOrientation=" + req + " width=" + w +" height="+ h +" resizable=" + resizable + " hint=" + hint);
        // The manifest already fixes this activity to landscape. Re-requesting
        // orientation while SDL is starting destroys the SurfaceView on some
        // Android 15 devices, leaving Vulkan without an ANativeWindow.
    }
}
