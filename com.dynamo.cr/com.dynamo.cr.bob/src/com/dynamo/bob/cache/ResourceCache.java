// Copyright 2021 The Defold Foundation
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

package com.dynamo.bob.cache;

import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.net.MalformedURLException;
import java.nio.file.Files;

import com.dynamo.bob.Bob;
import com.dynamo.bob.util.HttpUtil;

public class ResourceCache {

	private String localCacheDir;

	private String remoteCacheUrl;

	private HttpUtil http = new HttpUtil();

	private boolean enabled = false;

	public ResourceCache() {}

	public void init(String localCacheDir, String remoteCacheUrl) {
		Bob.verbose("Initialising resource cache with local cache dir '%s' and remote '%s'", localCacheDir, remoteCacheUrl);
		this.localCacheDir = localCacheDir;
		this.remoteCacheUrl = remoteCacheUrl;
		this.enabled = localCacheDir != null;
		if (localCacheDir != null) {
			File f = new File(localCacheDir);
			if (!f.exists()) {
				f.mkdirs();
			}
		}
	}

	public void setRemoteAuthentication(String user, String pass) {
		http.setAuthentication(user, pass);
	}

	private File fileFromKey(String key) {
		return new File(localCacheDir, key);
	}

	private URL urlFromFile(File file) throws MalformedURLException {
		return new URL(remoteCacheUrl + "/" + file.getName());
	}

	private void saveToLocalCache(File file, byte[] data) throws IOException {
		Bob.verbose("Resource '%s' saved to the local cache", file);
		Files.write(file.toPath(), data);
	}

	private byte[] loadFromLocalCache(File file) throws IOException {
		if (file.exists()) {
			Bob.verbose("Resource '%s' loaded from the local cache", file);
			return Files.readAllBytes(file.toPath());
		}
		return null;
	}

	private void uploadToRemoteCache(File file) throws MalformedURLException {
		if (remoteCacheUrl == null) {
			return;
		}
		if (!file.exists()) {
			return;
		}
		URL url = urlFromFile(file);
		if (!http.exists(url)) {
			http.uploadFile(url, file);
			Bob.verbose("Resource '%s' uploaded to the remote cache", file);
		}
		else {
			Bob.verbose("Resource '%s' already exists in the remote cache", file);
		}
	}

	private void downloadFromRemoteCache(File file) throws MalformedURLException {
		if (remoteCacheUrl == null) {
			return;
		}
		URL url = urlFromFile(file);
		if (http.exists(url)) {
			http.downloadToFile(url, file);
			Bob.verbose("Resource '%s' downloaded from the remote cache", file);
		}
		else {
			Bob.verbose("Resource '%s' does not exist in the remote cache", file);
		}
	}

	/**
	 * Put data in the resource cache
	 * @param key Key to associate data with
	 * @param data The data to store
	 */
	public void put(String key, byte[] data) throws IOException {
		if (!enabled) {
			return;
		}
		File file = fileFromKey(key);
		if (file.exists()) {
			// file is already in the local cache
			return;
		}

		Bob.verbose("Caching resource '%s'", file);
		saveToLocalCache(file, data);
		uploadToRemoteCache(file);
	}

	/**
	 * Get data from the resource cache
	 * @param key Key associated with the data to get
	 * @return The data or null if no data exists in the cache
	 */
	public byte[] get(String key) throws IOException {
		if (!enabled) {
			return null;
		}
		File file = fileFromKey(key);
		if (!file.exists()) {
			downloadFromRemoteCache(file);
		}

		return loadFromLocalCache(file);
	}

	public boolean contains(String key) {
		if (!enabled) {
			return false;
		}
		return fileFromKey(key).exists();
	}
}
