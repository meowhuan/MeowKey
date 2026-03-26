use anyhow::{Result, bail};
use serde_json::{Map, Number, Value};

#[derive(Clone, Debug, PartialEq)]
pub enum CborValue {
    Integer(i64),
    Bytes(Vec<u8>),
    Text(String),
    Array(Vec<CborValue>),
    Map(Vec<(CborValue, CborValue)>),
    Bool(bool),
    Null,
}

impl CborValue {
    pub fn to_json(&self) -> Value {
        match self {
            Self::Integer(value) => Value::Number(Number::from(*value)),
            Self::Bytes(bytes) => Value::String(format!("hex:{}", bytes_to_hex(bytes))),
            Self::Text(text) => Value::String(text.clone()),
            Self::Array(values) => Value::Array(values.iter().map(Self::to_json).collect()),
            Self::Map(entries) => {
                let mut output = Map::new();
                for (key, value) in entries {
                    output.insert(cbor_key_to_string(key), value.to_json());
                }
                Value::Object(output)
            }
            Self::Bool(value) => Value::Bool(*value),
            Self::Null => Value::Null,
        }
    }

    pub fn as_bytes(&self) -> Option<&[u8]> {
        match self {
            Self::Bytes(bytes) => Some(bytes),
            _ => None,
        }
    }

    pub fn as_text(&self) -> Option<&str> {
        match self {
            Self::Text(text) => Some(text),
            _ => None,
        }
    }

    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Self::Bool(value) => Some(*value),
            _ => None,
        }
    }

    pub fn as_array(&self) -> Option<&[CborValue]> {
        match self {
            Self::Array(values) => Some(values),
            _ => None,
        }
    }

    pub fn get_int_key(&self, needle: i64) -> Option<&CborValue> {
        match self {
            Self::Map(entries) => entries.iter().find_map(|(key, value)| {
                matches!(key, Self::Integer(found) if *found == needle).then_some(value)
            }),
            _ => None,
        }
    }

    pub fn get_text_key(&self, needle: &str) -> Option<&CborValue> {
        match self {
            Self::Map(entries) => entries.iter().find_map(|(key, value)| {
                matches!(key, Self::Text(found) if found == needle).then_some(value)
            }),
            _ => None,
        }
    }
}

pub fn encode(value: &CborValue) -> Result<Vec<u8>> {
    let mut output = Vec::new();
    encode_value(value, &mut output)?;
    Ok(output)
}

pub fn decode(bytes: &[u8]) -> Result<(CborValue, usize)> {
    let mut offset = 0usize;
    let value = decode_value(bytes, &mut offset)?;
    Ok((value, offset))
}

fn encode_value(value: &CborValue, output: &mut Vec<u8>) -> Result<()> {
    match value {
        CborValue::Integer(number) if *number >= 0 => write_length(0, *number as u64, output),
        CborValue::Integer(number) => write_length(1, number.unsigned_abs() - 1, output),
        CborValue::Bytes(bytes) => {
            write_length(2, bytes.len() as u64, output);
            output.extend_from_slice(bytes);
        }
        CborValue::Text(text) => {
            write_length(3, text.len() as u64, output);
            output.extend_from_slice(text.as_bytes());
        }
        CborValue::Array(values) => {
            write_length(4, values.len() as u64, output);
            for item in values {
                encode_value(item, output)?;
            }
        }
        CborValue::Map(entries) => {
            write_length(5, entries.len() as u64, output);
            for (key, value) in entries {
                encode_value(key, output)?;
                encode_value(value, output)?;
            }
        }
        CborValue::Bool(true) => output.push(0xf5),
        CborValue::Bool(false) => output.push(0xf4),
        CborValue::Null => output.push(0xf6),
    }
    Ok(())
}

fn decode_value(bytes: &[u8], offset: &mut usize) -> Result<CborValue> {
    let initial = *bytes
        .get(*offset)
        .ok_or_else(|| anyhow::anyhow!("CBOR 数据意外结束"))?;
    *offset += 1;

    let major_type = initial >> 5;
    let additional_info = initial & 0x1f;

    match major_type {
        0 => Ok(CborValue::Integer(
            read_length(bytes, offset, additional_info)? as i64,
        )),
        1 => {
            let value = read_length(bytes, offset, additional_info)? as i64;
            Ok(CborValue::Integer(-value - 1))
        }
        2 => {
            let length = read_length(bytes, offset, additional_info)? as usize;
            let end = *offset + length;
            let value = bytes
                .get(*offset..end)
                .ok_or_else(|| anyhow::anyhow!("CBOR 字节串越界"))?
                .to_vec();
            *offset = end;
            Ok(CborValue::Bytes(value))
        }
        3 => {
            let length = read_length(bytes, offset, additional_info)? as usize;
            let end = *offset + length;
            let value = std::str::from_utf8(
                bytes
                    .get(*offset..end)
                    .ok_or_else(|| anyhow::anyhow!("CBOR 文本越界"))?,
            )?
            .to_string();
            *offset = end;
            Ok(CborValue::Text(value))
        }
        4 => {
            let length = read_length(bytes, offset, additional_info)? as usize;
            let mut values = Vec::with_capacity(length);
            for _ in 0..length {
                values.push(decode_value(bytes, offset)?);
            }
            Ok(CborValue::Array(values))
        }
        5 => {
            let length = read_length(bytes, offset, additional_info)? as usize;
            let mut entries = Vec::with_capacity(length);
            for _ in 0..length {
                let key = decode_value(bytes, offset)?;
                let value = decode_value(bytes, offset)?;
                entries.push((key, value));
            }
            Ok(CborValue::Map(entries))
        }
        7 if additional_info == 20 => Ok(CborValue::Bool(false)),
        7 if additional_info == 21 => Ok(CborValue::Bool(true)),
        7 if additional_info == 22 => Ok(CborValue::Null),
        _ => bail!("暂不支持的 CBOR 主类型 {major_type} / 附加信息 {additional_info}"),
    }
}

fn read_length(bytes: &[u8], offset: &mut usize, additional_info: u8) -> Result<u64> {
    match additional_info {
        0..=23 => Ok(additional_info as u64),
        24 => {
            let value = *bytes
                .get(*offset)
                .ok_or_else(|| anyhow::anyhow!("CBOR 缺少 8 位长度"))?;
            *offset += 1;
            Ok(value as u64)
        }
        25 => {
            let value = u16::from_be_bytes(
                bytes
                    .get(*offset..(*offset + 2))
                    .ok_or_else(|| anyhow::anyhow!("CBOR 缺少 16 位长度"))?
                    .try_into()
                    .expect("slice length already checked"),
            );
            *offset += 2;
            Ok(value as u64)
        }
        26 => {
            let value = u32::from_be_bytes(
                bytes
                    .get(*offset..(*offset + 4))
                    .ok_or_else(|| anyhow::anyhow!("CBOR 缺少 32 位长度"))?
                    .try_into()
                    .expect("slice length already checked"),
            );
            *offset += 4;
            Ok(value as u64)
        }
        _ => bail!("暂不支持的 CBOR 附加信息 {additional_info}"),
    }
}

fn write_length(major_type: u8, value: u64, output: &mut Vec<u8>) {
    if value < 24 {
        output.push((major_type << 5) | value as u8);
    } else if value < 0x100 {
        output.push((major_type << 5) | 24);
        output.push(value as u8);
    } else if value < 0x1_0000 {
        output.push((major_type << 5) | 25);
        output.extend_from_slice(&(value as u16).to_be_bytes());
    } else {
        output.push((major_type << 5) | 26);
        output.extend_from_slice(&(value as u32).to_be_bytes());
    }
}

fn cbor_key_to_string(value: &CborValue) -> String {
    match value {
        CborValue::Integer(number) => number.to_string(),
        CborValue::Text(text) => text.clone(),
        _ => value.to_json().to_string(),
    }
}

fn bytes_to_hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}
