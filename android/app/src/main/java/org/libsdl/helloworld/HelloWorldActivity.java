package org.libsdl.helloworld;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class HelloWorldActivity extends SDLActivity {

    private static final String TAG = "gknext";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        copyAssetsToExternalStorage();
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
        // 从 assets 的根目录 ("") 开始拷贝
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
        byte[] buffer = new byte[4096]; // 4KB buffer
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "gkNextRenderer" };
    }
}