import requests

MY_PROXY_PORT = 50123  # CHANGE ME!

proxies = {
    "http": f"http://127.0.0.1:{MY_PROXY_PORT}",
    "https": f"http://127.0.0.1:{MY_PROXY_PORT}",
}

# headers = {
#     "Host": "www.example.com",
#     "User-Agent": "curl/7.88.1",
#     "Accept": "*/*",
#     "Proxy-Connection": "Keep-Alive",
#     "Accept-Encoding": "identity"
# }

headers = {
    "Host": "www.example.com",
    "User-Agent": "python-requests/2.28.1",
    "Accept-Encoding": "gzip, deflate, br",
    "Accept": "*/*",
    "Connection": "keep-alive"
}
# Make the GET request
resp = requests.get("http://www.example.com/", headers=headers, proxies=proxies)

# Print the response status and headers
print(f"HTTP/{resp.raw.version/10:.1f} {resp.status_code} {resp.reason}")
for key, value in resp.headers.items():
    print(f"{key}: {value}")

# Optional: print response body
print("\n" + resp.text)

# # GET request over HTTP
# response = requests.get("http://www.example.org", proxies=proxies, timeout=10)
# assert response.status_code == 200
# assert "Example Domain" in response.text

# # GET request over HTTPS
# response = requests.get("https://www.example.org", proxies=proxies, timeout=10)
# assert response.status_code == 200
# assert "Example Domain" in response.text

# print("All tests passed!")