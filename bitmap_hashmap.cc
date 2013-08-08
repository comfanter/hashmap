#include "bitmap_hashmap.h"

namespace hhash {


int BitmapHashMap::CheckDensity() {
  int num_pages = 0;
  int count_empty = 0;
  int count_probe = 0;

  int level = 32;

  for (uint32_t i = 0; i < num_buckets_; i++) {
    if (buckets_[i].entry == NULL) {
      count_empty += 1;
    } else {
      count_probe += 1; 
    }

    if (i > 0 && i % level == 0) {
      if (count_probe < 0.25 * level) {
        std::cout << ".";
      } else if (count_probe < 0.5 * level) {
        std::cout << ":";
      } else if (count_probe < 0.75 * level) {
        std::cout << "|";
      } else if (count_probe < 0.85 * level) {
        std::cout << "o";
      } else if (count_probe < 0.95 * level) {
        std::cout << "U";
      } else if (count_probe < level) {
        std::cout << "O";
      } else {
        std::cout << "0";
      }
      count_probe = 0;
      num_pages += 1;
    }
  }
  std::cout << std::endl;

  std::cout << "Count empty: " << count_empty << "/" << num_buckets_ << std::endl;
  std::cout << "Pages: " << num_pages << " | " << num_pages * level << std::endl;
  return 0;
}


int BitmapHashMap::BucketCounts() {
  int counts[33];
  for (int i = 0; i <= 32; i++) {
    counts[i] = 0;
  }

  int total = 0;

  for (uint32_t i = 0; i < num_buckets_; i++) {
    counts[hamming2(buckets_[i].bitmap)] += 1;
  }

  for (int i = 0; i <= 32; i++) {
    std::cout << "size " << i << ": " << counts[i] << std::endl;
    total += counts[i];
  }

  std::cout << "total: " << total << std::endl;

  return 0;
}



int BitmapHashMap::Dump() {
  for (uint32_t i = 0; i < num_buckets_ + size_neighborhood_; i++) {

    std::cout << "bitmap: ";
    for (uint32_t j = 0; j < size_neighborhood_; j++) {
      uint32_t mask = 1 << (size_neighborhood_-1-j);
      if (buckets_[i].bitmap & mask) {
        std::cout << "1";
      } else {
        std::cout << "0";
      }
    }

    if (buckets_[i].entry != NULL) {
      std::string key(buckets_[i].entry->data,
                      buckets_[i].entry->size_key);
      std::string value(buckets_[i].entry->data + buckets_[i].entry->size_key,
                        buckets_[i].entry->size_value);
      std::cout << " | index: " << i << "  - " << key << " " << value;
    }
    std::cout << std::endl;
  }
  return 0;
}





int BitmapHashMap::Open() {
  buckets_ = new Bucket[num_buckets_ + size_neighborhood_];
  memset(buckets_, 0, sizeof(Bucket) * (num_buckets_ + size_neighborhood_));
  return 0;
}

int BitmapHashMap::Get(const std::string& key, std::string* value) {
  uint64_t hash = hash_function(key);
  uint64_t index_init = hash % num_buckets_;
  uint32_t mask = 1 << (size_neighborhood_-1);
  bool found = false;
  for (uint32_t i = 0; i < size_neighborhood_; i++) {
    if (buckets_[index_init].bitmap & mask) {
      uint64_t index_current = (index_init + i) % num_buckets_;
      if (   buckets_[index_current].entry != NULL
          && key.size() == buckets_[index_current].entry->size_key
          && memcmp(buckets_[index_current].entry->data, key.c_str(), key.size()) == 0) {
        *value = std::string(buckets_[index_current].entry->data + key.size(),
                             buckets_[index_current].entry->size_value);
        //std::cout << out << std::endl;
        found = true;
        break;
      }
    }
    mask = mask >> 1;
  }

  if (found) return 0;

  return 1;
}

uint64_t BitmapHashMap::FindEmptyBucket(uint64_t index_init) {
  bool found = false;
  uint64_t index_current = index_init;
  for (uint32_t i = 0; i < size_probing_; i++) {
    index_current = (index_init + i) % num_buckets_;
    if (buckets_[index_current].entry == NULL) {
      found = true;
      break;
    }
  }

  if (!found) {
    std::cerr << "Put(): cannot resize" << std::endl; 
    return num_buckets_;
  }

  uint64_t index_empty = index_current;
  while (   (index_empty >= index_init && (index_empty - index_init) >= size_neighborhood_)
         || (index_empty <  index_init && (index_empty + num_buckets_ - index_init) >= size_neighborhood_)) {
    uint64_t index_base_init = (num_buckets_ + index_empty - (size_neighborhood_ - 1)) % num_buckets_;
    // For each candidate base bucket
    bool found_swap = false;
    for (uint32_t i = 0; i < size_neighborhood_ - 1; i++) {
      // -1 because no need to test the bucket at index_empty
      // For each mask position
      uint32_t index_base = (index_base_init + i) % num_buckets_;
      uint32_t mask = 1 << (size_neighborhood_-1);
      for (uint32_t j = 0; j < size_neighborhood_ - i - 1; j++) {
        if (buckets_[index_base].bitmap & mask) {
          // Found, so now we swap buckets and update the bitmap
          uint32_t index_candidate = (index_base + j) % num_buckets_;
          buckets_[index_empty].entry = buckets_[index_candidate].entry;
          buckets_[index_base].bitmap &= ~mask;
          uint32_t mask_new = 1 << i;
          buckets_[index_base].bitmap |= mask_new;
          index_empty = index_candidate;
          found_swap = true;
          //std::cerr << "swapping!" << std::endl;
          break;
        }
        mask = mask >> 1;
      }
      if (found_swap) break;
    }
    if (!found_swap) {
      std::cerr << "Put(): cannot resize" << std::endl;
      return num_buckets_;
    }
  }

  return index_empty;
}

int BitmapHashMap::Put(const std::string& key, const std::string& value) {
  uint64_t hash = hash_function(key);
  uint64_t index_init = hash % num_buckets_;
  uint64_t index_empty = FindEmptyBucket(index_init);

  if (index_empty == num_buckets_) {
    return 1; 
  }

  char *data = new char[key.size() + value.size()];
  memcpy(data, key.c_str(), key.size());
  memcpy(data + key.size(), value.c_str(), value.size());

  BitmapHashMap::Entry *entry = new BitmapHashMap::Entry;
  entry->size_key = key.size();
  entry->size_value = value.size();
  entry->data = data;
  buckets_[index_empty].entry = entry;

  uint32_t mask;
  if (index_empty >= index_init) {
    mask = 1 << size_neighborhood_ - ((index_empty - index_init) + 1);
  } else {
    mask = 1 << size_neighborhood_ - ((index_empty + num_buckets_ - index_init) + 1);
  }
  buckets_[index_init].bitmap |= mask; 

  return 0;
}

int BitmapHashMap::Exists(const std::string& key) {
  return 0;
}

int BitmapHashMap::Remove(const std::string& key) {
  return 0;
}


};
