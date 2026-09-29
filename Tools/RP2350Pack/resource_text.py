"""Text payloads requiring an addressable trailing NUL in the XIP image."""
TEXT_SUFFIXES = ('.cfg', '.rc', '.txt', '.ent')
def is_text_resource(name):
    if isinstance(name, bytes):
        name=name.decode('ascii')
    return name.lower().endswith(TEXT_SUFFIXES)
