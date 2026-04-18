#!/usr/bin/env python3
"""
Markdown to Nexus Mods BBCode Converter
"""

import re
import sys
from pathlib import Path


def convert_md_to_bbcode(md_text: str) -> str:
    """Convert Markdown text to Nexus Mods BBCode format."""
    text = md_text
    
    # Normalize line endings
    text = text.replace('\r\n', '\n')
    
    # Code blocks (do these first to protect their contents)
    code_blocks = []
    def save_code_block(match):
        code_blocks.append(match.group(2))
        return f"%%CODEBLOCK{len(code_blocks) - 1}%%"
    
    text = re.sub(r'```(\w*)\n(.*?)```', save_code_block, text, flags=re.DOTALL)
    
    # Inline code (protect before other processing)
    inline_codes = []
    def save_inline_code(match):
        inline_codes.append(match.group(1))
        return f"%%INLINECODE{len(inline_codes) - 1}%%"
    
    text = re.sub(r'`([^`]+)`', save_inline_code, text)
    
    # Headers (Nexus uses size tags)
    text = re.sub(r'^######\s+(.+)$', r'[b]\1[/b]', text, flags=re.MULTILINE)
    text = re.sub(r'^#####\s+(.+)$', r'[b]\1[/b]', text, flags=re.MULTILINE)
    text = re.sub(r'^####\s+(.+)$', r'[size=3][b]\1[/b][/size]', text, flags=re.MULTILINE)
    text = re.sub(r'^###\s+(.+)$', r'[size=4][b]\1[/b][/size]', text, flags=re.MULTILINE)
    text = re.sub(r'^##\s+(.+)$', r'[size=5][b]\1[/b][/size]', text, flags=re.MULTILINE)
    text = re.sub(r'^#\s+(.+)$', r'[size=6][b]\1[/b][/size]', text, flags=re.MULTILINE)
    
    # Bold and italic combinations
    text = re.sub(r'\*\*\*(.+?)\*\*\*', r'[b][i]\1[/i][/b]', text)
    text = re.sub(r'___(.+?)___', r'[b][i]\1[/i][/b]', text)
    
    # Bold
    text = re.sub(r'\*\*(.+?)\*\*', r'[b]\1[/b]', text)
    text = re.sub(r'__(.+?)__', r'[b]\1[/b]', text)
    
    # Italic
    text = re.sub(r'\*(.+?)\*', r'[i]\1[/i]', text)
    text = re.sub(r'_(.+?)_', r'[i]\1[/i]', text)
    
    # Strikethrough
    text = re.sub(r'~~(.+?)~~', r'[s]\1[/s]', text)
    
    # Links: [text](url)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'[url=\2]\1[/url]', text)
    
    # Images: ![alt](url)
    text = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'[img]\2[/img]', text)
    
    # Blockquotes
    text = re.sub(r'^>\s+(.+)$', r'[quote]\1[/quote]', text, flags=re.MULTILINE)
    
    # Horizontal rules
    text = re.sub(r'^[-*_]{3,}\s*$', r'[line]', text, flags=re.MULTILINE)
    
    # Unordered lists
    def convert_unordered_list(match):
        items = match.group(0)
        list_items = re.findall(r'^[\-\*\+]\s+(.+)$', items, flags=re.MULTILINE)
        if list_items:
            result = '[list]\n'
            for item in list_items:
                result += f'[*]{item}\n'
            result += '[/list]'
            return result
        return items
    
    text = re.sub(r'(^[\-\*\+]\s+.+$\n?)+', convert_unordered_list, text, flags=re.MULTILINE)
    
    # Ordered lists
    def convert_ordered_list(match):
        items = match.group(0)
        list_items = re.findall(r'^\d+\.\s+(.+)$', items, flags=re.MULTILINE)
        if list_items:
            result = '[list=1]\n'
            for item in list_items:
                result += f'[*]{item}\n'
            result += '[/list]'
            return result
        return items
    
    text = re.sub(r'(^\d+\.\s+.+$\n?)+', convert_ordered_list, text, flags=re.MULTILINE)
    
    # Restore code blocks
    for i, code in enumerate(code_blocks):
        text = text.replace(f"%%CODEBLOCK{i}%%", f"[code]{code}[/code]")
    
    # Restore inline code
    for i, code in enumerate(inline_codes):
        text = text.replace(f"%%INLINECODE{i}%%", f"[font=Courier New]{code}[/font]")
    
    # Clean up extra blank lines
    text = re.sub(r'\n{3,}', '\n\n', text)
    
    return text.strip()


def main():
    if len(sys.argv) < 2:
        print("Usage: python md_to_bbcode.py <input.md> [output.txt]")
        print("       If output is not specified, prints to stdout")
        sys.exit(1)
    
    input_path = Path(sys.argv[1])
    
    if not input_path.exists():
        print(f"Error: File not found: {input_path}")
        sys.exit(1)
    
    md_content = input_path.read_text(encoding='utf-8')
    bbcode = convert_md_to_bbcode(md_content)
    
    if len(sys.argv) >= 3:
        output_path = Path(sys.argv[2])
        output_path.write_text(bbcode, encoding='utf-8')
        print(f"Converted: {input_path} -> {output_path}")
    else:
        print(bbcode)


if __name__ == "__main__":
    main()