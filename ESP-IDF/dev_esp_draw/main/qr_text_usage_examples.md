# QR+Text Command Usage Examples

This document provides examples of how to use the new `qr_text` command type that displays QR codes with accompanying text on the same screen.

## Command Format

```json
{
  "type": "qr_text",
  "data": "QR_CODE_DATA_HERE",
  "text": "Text to display below QR code",
  "qr_color": "#000000",
  "bg_color": "#FFFFFF", 
  "text_color": "#0000FF"
}
```

## Required Fields

- `type`: Must be `"qr_text"`
- `data`: The data to encode in the QR code (string)

## Optional Fields

- `text`: Text to display below the QR code
- `qr_color`: Color of the QR code (hex format, default: black)
- `bg_color`: Background color of the screen (hex format, default: white)
- `text_color`: Color of the text (hex format, default: blue)
- `font_size`: Size of the text font in pixels (range: 10-32, default: 18)

## Examples

### Basic QR Code with Text
```json
{
  "type": "qr_text",
  "data": "https://example.com",
  "text": "Visit our website!"
}
```

### Custom Colors
```json
{
  "type": "qr_text",
  "data": "WIFI:T:WPA;S:MyNetwork;P:password123;;",
  "text": "WiFi Network Credentials",
  "qr_color": "#FF0000",
  "bg_color": "#FFFF00",
  "text_color": "#0000FF"
}
```

### QR Code Only (no text)
```json
{
  "type": "qr_text",
  "data": "product-12345"
}
```

### Contact Information
```json
{
  "type": "qr_text",
  "data": "BEGIN:VCARD\nVERSION:3.0\nFN:John Doe\nTEL:+1234567890\nEMAIL:john@example.com\nEND:VCARD",
  "text": "John Doe - Contact Card"
}
```

### Payment Information
```json
{
  "type": "qr_text",
  "data": "bitcoin:1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa?amount=0.001",
  "text": "Bitcoin Payment Address\nSend 0.001 BTC"
}
```

### Font Size Examples

#### Small Font (10px)
```json
{
  "type": "qr_text",
  "data": "https://example.com/small",
  "text": "Small text for compact display",
  "font_size": 10
}
```

#### Medium Font (18px - default)
```json
{
  "type": "qr_text",
  "data": "https://example.com/medium", 
  "text": "Medium text - standard size",
  "font_size": 18
}
```

#### Large Font (24px)
```json
{
  "type": "qr_text",
  "data": "https://example.com/large",
  "text": "Large text for better readability",
  "font_size": 24
}
```

#### Extra Large Font (32px)
```json
{
  "type": "qr_text",
  "data": "https://example.com/xlarge",
  "text": "Extra large text for maximum visibility",
  "font_size": 32
}
```

### Complete Example with All Options
```json
{
  "type": "qr_text",
  "data": "WIFI:T:WPA;S:MyNetwork;P:password123;;",
  "text": "WiFi Network\nScan to connect",
  "qr_color": "#FF0000",
  "bg_color": "#FFFF00", 
  "text_color": "#0000FF",
  "font_size": 20
}
```

## Font Size Information

Supported font sizes (in pixels):
- **10px**: Very small text - `lv_font_montserrat_10`
- **14px**: Small text - `lv_font_montserrat_14`  
- **16px**: Small-medium text - `lv_font_montserrat_16`
- **18px**: Standard/Default size - `lv_font_montserrat_18`
- **20px**: Medium-large text - `lv_font_montserrat_20`
- **24px**: Large text - `lv_font_montserrat_24`
- **28px**: Extra large text - `lv_font_montserrat_28`
- **32px**: Very large text - `lv_font_montserrat_32`

**Note**: If you specify a size outside the 10-32px range, it will be automatically clamped to the nearest valid value. For intermediate values (e.g., 15px, 19px), the system will select the closest available font size.

## Color Format

Colors should be specified in hex format:
- `#RRGGBB` (e.g., `#FF0000` for red)
- Can also be without `#` prefix: `FF0000`

## Layout Information

- QR code is positioned at 20% from the top of the screen
- QR code size is 70% of screen width
- Text is displayed below the QR code with 30px margin
- Text is center-aligned and spans almost full screen width (with 20px margins)

## Error Handling

If QR code generation fails, the system will display an error message:
- "QR+Text Generation Failed" - When QR code creation fails
- "QR Code Support Disabled" - When QR support is compiled out

## Integration with Existing Commands

The `qr_text` command integrates seamlessly with existing commands:
- Use `"type": "clear"` to clear the screen
- Use `"type": "text"` for text-only display
- Use `"type": "qr"` for QR-only display

Each command type automatically cleans up previously displayed content.
