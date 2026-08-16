#!/usr/bin/env python3
"""create-libcage-pro-payment-link.py — Stripe Payment Link for Libcage Pro ($99)."""
import os, sys, json, base64, urllib.request, urllib.error, urllib.parse

ENV = "/home/scott/.local/etc/hardonia-checkout-api.env"
def _key():
    for line in open(ENV):
        if line.startswith("STRIPE_SECRET_KEY="):
            return line.split("=",1)[1].strip().strip('"\'')
    return ""
KEY = _key()
if not KEY or not KEY.startswith("sk_live_"):
    print("ERROR: no live Stripe key", file=sys.stderr); sys.exit(1)

def _post(path, payload):
    auth = "Basic " + base64.b64encode((KEY + ":").encode()).decode()
    data = urllib.parse.urlencode(payload).encode()
    req = urllib.request.Request("https://api.stripe.com/v1"+path, data=data,
        headers={"Authorization": auth, "User-Agent": "libcage-setup/1.0"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=30))
    except urllib.error.HTTPError as e:
        print("STRIPE ERROR:", e.read().decode()[:300], file=sys.stderr); sys.exit(1)

prod = _post("/products", {"name": "Libcage Pro — SBOM + Policy Engine",
        "description": "Paid tier: CycloneDX SBOM generation, team endpoint-policy enforcement, audit-gated features."})
price = _post("/prices", {"product": prod["id"], "currency": "usd", "unit_amount": "9900", "lookup_key": "libcage-pro"})
link = _post("/payment_links", {"line_items[0][price]": price["id"], "line_items[0][quantity]": "1",
        "after_completion[type]": "redirect",
        "after_completion[redirect][url]": "https://aiautomatedsystems.ca/p/libcage-pro"})
print(json.dumps({"product_id": prod["id"], "price_id": price["id"],
        "payment_link": link["url"], "payment_link_id": link["id"]}, indent=2))
