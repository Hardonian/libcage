#!/usr/bin/env python3
"""create-libcage-payment-link.py — create a Stripe Payment Link for libcage ($29 one-time).

Reads the live secret key from the checkout-api env file (never echoes it).
Creates a Product + Price + Payment Link, prints the buy URL.
This is the legitimate monetization step for the libcage product.
"""
import os, sys, json, base64, urllib.request, urllib.error

ENV = "/home/scott/.local/etc/hardonia-checkout-api.env"
def _key():
    for line in open(ENV):
        if line.startswith("STRIPE_SECRET_KEY="):
            return line.split("=",1)[1].strip().strip('"\'')
    return ""

KEY = _key()
if not KEY or not KEY.startswith("sk_live_"):
    print("ERROR: no live Stripe key found", file=sys.stderr); sys.exit(1)

def _post(path, payload):
    auth = "Basic " + base64.b64encode((KEY + ":").encode()).decode()
    data = urllib.parse.urlencode(payload).encode()
    req = urllib.request.Request(
        "https://api.stripe.com/v1" + path,
        data=data, headers={"Authorization": auth, "User-Agent": "libcage-setup/1.0"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=30))
    except urllib.error.HTTPError as e:
        print("STRIPE ERROR:", e.read().decode()[:300], file=sys.stderr); sys.exit(1)

import urllib.parse
# 1. Product
prod = _post("/products", {"name": "Libcage — Pure-C LLM Agent Runtime",
                            "description": "Zero-dependency autonomous code-repair agent. Single static binary."})
# 2. Price ($29.00 one-time)
price = _post("/prices", {"product": prod["id"], "currency": "usd",
                          "unit_amount": "2900", "lookup_key": "libcage"})
# 3. Payment Link
link = _post("/payment_links", {"line_items[0][price]": price["id"],
                                 "line_items[0][quantity]": "1",
                                 "after_completion[type]": "redirect",
                                 "after_completion[redirect][url]": "https://aiautomatedsystems.ca/p/libcage"})
print(json.dumps({"product_id": prod["id"], "price_id": price["id"],
                  "payment_link": link["url"], "payment_link_id": link["id"]}, indent=2))
