#include "sys.h"
#include "CombinedImageSamplerUpdater.h"
#include "SynchronousWindow.h"
#include "pipeline/ShaderInputData.h"
#include "descriptor/TextureUpdateRequest.h"
#ifdef CWDEBUG
#include "vk_utils/print_pointer.h"
#include "utils/has_print_on.h"
#endif

namespace vulkan::descriptor {

void CombinedImageSamplerUpdater::prepare_shader_resource_declaration(descriptor::SetIndexHint set_index_hint, pipeline::ShaderInputData* shader_input_data) const
{
  shader_input_data->prepare_combined_image_sampler_declaration(*this, set_index_hint);
}

CombinedImageSamplerUpdater::~CombinedImageSamplerUpdater()
{
  DoutEntering(dc::statefultask(mSMDebug)|dc::vulkan, "CombinedImageSamplerUpdater::~CombinedImageSamplerUpdater() [" << this << "]");
}

char const* CombinedImageSamplerUpdater::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(CombinedImageSamplerUpdater_need_action);
    AI_CASE_RETURN(CombinedImageSamplerUpdater_done);
  }
  AI_NEVER_REACHED
}

std::pair<CombinedImageSamplerUpdater::factory_characteristic_key_to_descriptor_t::const_iterator, CombinedImageSamplerUpdater::factory_characteristic_key_to_descriptor_t::const_iterator>
CombinedImageSamplerUpdater::find_descriptors(pipeline::FactoryCharacteristicKey const& key) const
{
  auto begin = m_factory_characteristic_key_to_descriptor.begin();
  auto end = m_factory_characteristic_key_to_descriptor.end();
  while (begin != m_factory_characteristic_key_to_descriptor.end())
  {
    if (begin->first == key)    // Returns true iff the ids match and the subranges overlap.
    {
      end = begin + 1;
      break;
    }
    ++begin;
  }
  while (end != m_factory_characteristic_key_to_descriptor.end())
  {
    if (end->first != key)
      break;
    ++end;
  }
  return {begin, end};
}

CombinedImageSamplerUpdater::factory_characteristic_key_to_texture_t::const_iterator CombinedImageSamplerUpdater::find_texture(pipeline::FactoryCharacteristicKey const& key) const
{
  factory_characteristic_key_to_texture_t::const_iterator result = m_factory_characteristic_key_to_texture.begin();
  while (result != m_factory_characteristic_key_to_texture.end())
  {
    if (result->first == key)   // Returns true iff the ids match and the subranges overlap.
      break;
    ++result;
  }
#if CW_DEBUG
  if (result != m_factory_characteristic_key_to_texture.end())
  {
    auto end = result + 1;
    if (end != m_factory_characteristic_key_to_texture.end() && end->first == key)
      DoutFatal(dc::core, "CombinedImageSampler::find_texture: key (" << key << ") matches multiple textures (at least " << *result << " and " << *end << ").");
  }
#endif
  return result;
}

void CombinedImageSamplerUpdater::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case CombinedImageSamplerUpdater_need_action:
      // Get all the new descriptors that need updating from the TaskToTaskDeque.
      flush_new_data([this](boost::intrusive_ptr<Update>&& update){
          Dout(dc::always, "Received: " << *update << " on " << this << " (" << debug_name() << ")");
          if (update->is_descriptor_update_info())
          {
            DescriptorUpdateInfo const* descriptor_update_info = static_cast<DescriptorUpdateInfo const*>(update.get());
            // All DescriptorUpdateInfo's must refer to the window that owns this CombinedImageSamplerUpdater.
            ASSERT(!m_owning_window || m_owning_window == descriptor_update_info->owning_window());
            m_owning_window = descriptor_update_info->owning_window();
            pipeline::FactoryCharacteristicKey key = descriptor_update_info->key();

            // Look for an existing entry with the same FactoryCharacteristicId.

            // m_factory_characteristic_key_to_descriptor is vector storing std::pair<FactoryCharacteristicKey, FactoryCharacteristicData>.
            // It is sorted by the FactoryCharacteristicKey, where
            //
            //                                                      PipelineFactoryIndex          Primary sorting key.
            //                            FactoryCharacteristicId <
            // FactoryCharacteristicKey <                           CharacteristicRangeIndex      Secondary sorting key.
            //                            ConsecutiveRange                                        Tertiary sorting key.
            //
            // The stored ConsecutiveRange's, for equal FactoryCharacteristicId's, may not overlap (but there may be gaps).

            // Searching can therefore be done by looking for the first element in m_factory_characteristic_key_to_descriptor
            // that is not less than the key we're looking for, and then checking that the key is not less than the
            // the element we found.
            //
            // For example: consider having a list with id's { 3, 5, 8 } and wanting to insert a 6.
            // Then we'd loop until !(8 < 6); or if we wanted to insert a 5, we'd loop until !(5 < 5).
            // Finally if we wanted to insert a 9, we'd loop until iter == end.

            // Note: 'factory_characteristic' stands for "PipelineFactory/CharacteristicRange (index) pair";
            // while 'subrange' stands for a consecutive (sub)range of that CharacteristicRange.
            factory_characteristic_key_to_descriptor_t::iterator iter = m_factory_characteristic_key_to_descriptor.begin();
            while (iter != m_factory_characteristic_key_to_descriptor.end())
            {
              // m_factory_characteristic_key_to_descriptor is sorted with an increasing id,
              // so as long as our id is less we didn't find it yet, but still have a chance.
              if (!(iter->first.id() < descriptor_update_info->factory_characteristic_id()))    // Stop at 8 < 6, or 5 < 5, never stop here for ? < 9.
                break;
              ++iter;
            }
            if (iter == m_factory_characteristic_key_to_descriptor.end() ||                     // To catch that that 9 didn't exist.
                descriptor_update_info->factory_characteristic_id() < iter->first.id())         // Compare 6 < 8 to find out that 6 didn't exist.
            {
              // This is the first entry with this factory_characteristic_id. Insert it before iter.
              iter = m_factory_characteristic_key_to_descriptor.insert(iter,
                  factory_characteristic_key_to_descriptor_t::value_type{key, descriptor_update_info->data()});
            }
            else                                                                // Otherwise we stopped at 5 which is equal to what we have.
            {
              // We found a (the first) key with the same id.
              //
              // Next look for the first element with this key, if any, that has a subrange that is larger than our fill_index.
              //
              // Consider trying to insert [id=A, fill_index=4] into
              // ...[id=A; subrange=[0, 3>], [id=A; subrange=[6, 7>], [id=A; subrange=[9, 12>, [id=B; subrange=...
              // Then result would be:
              // ...[id=A; subrange=[0, 3>], [id=A; subrange=[4, 5>], [id=A; subrange=[6, 7>], [id=A; subrange=[9, 12>, [id=B; subrange=...
              // if the new descriptor is new, or either
              // ...[id=A; subrange=[0, 5>], [id=A; subrange=[6, 7>], [id=A; subrange=[9, 12>, [id=B; subrange=...
              // or
              // ...[id=A; subrange=[0, 3>], [id=A; subrange=[4, 7>], [id=A; subrange=[9, 12>, [id=B; subrange=...
              // if the new descriptor matches the first or second entry respectively.
              // It is not allowed to match any other entries with id=A, because the subranges must be consective.
              factory_characteristic_key_to_descriptor_t::iterator prev;
              bool prev_valid = false;
              while (iter != m_factory_characteristic_key_to_descriptor.end())
              {
                if (descriptor_update_info->fill_index() < iter->first.subrange().begin())
                  break;
                prev = iter;
                prev_valid = true;
                ++iter;
              }
              factory_characteristic_key_to_descriptor_t::iterator curr = prev;
              for (int n = 0; n < 3; ++n)
              {
                // We first test prev (n==0), then iter (n==1) and finally (n==2) insert a new subrange if neither prev nor iter matched.
                if (n != 0 || prev_valid)
                {
                  if (n == 2)
                  {
                    // Insert the new 'subrange' in between prev and iter (aka, before iter).
                    curr = m_factory_characteristic_key_to_descriptor.insert(iter,
                        {descriptor_update_info->key(), { descriptor_update_info->descriptor_set(), descriptor_update_info->binding() }});
#if CW_DEBUG
                    // Now curr points to the entry that we added the new descriptor too.
                    // For all remaining entries with the same factory_characteristic_id make sure
                    // that the descriptors are different.
                    while (++curr != m_factory_characteristic_key_to_descriptor.end());
                    {
                      if (curr->first.id() != descriptor_update_info->factory_characteristic_id())
                        break;
                      // All fill_index with the same descriptor_set/binding values must be in a single subrange.
                      ASSERT(!(curr->second.descriptor_set().as_key() == descriptor_update_info->descriptor_set().as_key()) ||
                          curr->second.binding() != descriptor_update_info->binding());
                    }
#endif
                    break;
                  }
                  if (curr->second.descriptor_set().as_key() == descriptor_update_info->descriptor_set().as_key() &&
                      curr->second.binding() == descriptor_update_info->binding())
                  {
                    // Add the new entry to an existing one.
                    curr->first.extend_subrange(descriptor_update_info->fill_index());
                    // And visa versa: update the key with the total subrange.
                    key.set_subrange(curr->first.subrange());
                    break;
                  }
                }
                curr = iter;
              }
            }
            auto texture = find_texture(key);
            if (texture != m_factory_characteristic_key_to_texture.end())
              texture->second->update_descriptor_array(m_owning_window, descriptor_update_info->descriptor_set(), descriptor_update_info->binding(), { 0, 1 }); // FIXME: use real values for range
            else
              m_owning_window->update_descriptor_set_with_loading_texture(descriptor_update_info->descriptor_set(), descriptor_update_info->binding(), { 0, descriptor_update_info->descriptor_array_size() });

            Dout(dc::always, "m_factory_characteristic_key_to_descriptor is now: " << m_factory_characteristic_key_to_descriptor);
          }
          else
          {
            // We received a TextureUpdateRequest.
            TextureUpdateRequest const* texture_update_request = static_cast<TextureUpdateRequest const*>(update.get());
            pipeline::FactoryCharacteristicKey const key = texture_update_request->key();
            Texture const* texture = texture_update_request->texture();
            descriptor::ArrayElementRange const array_element_range = texture_update_request->array_element_range();

            // The following code works the same as for descriptors; see above for comments.
            factory_characteristic_key_to_texture_t::iterator iter = m_factory_characteristic_key_to_texture.begin();
            while (iter != m_factory_characteristic_key_to_texture.end())
            {
              if (!(iter->first.id() < texture_update_request->factory_characteristic_id()))
                break;
              ++iter;
            }
            if (iter == m_factory_characteristic_key_to_texture.end() ||
                texture_update_request->factory_characteristic_id() < iter->first.id())
            {
              // This is the first entry with this factory_characteristic_id. Insert it before iter.
              m_factory_characteristic_key_to_texture.insert(iter,
                  factory_characteristic_key_to_texture_t::value_type{key, texture});
            }
            else
            {
              // Loop over elements with the same FactoryCharacteristicId and increasing ConsecutiveRange.
              bool same_factory_characteristic_id = false;
              bool same_texture = false;
              while (iter != m_factory_characteristic_key_to_texture.end() && iter->first.id() == texture_update_request->factory_characteristic_id())
              {
                same_texture = iter->second == texture;
                // Consider a vector with the subranges {[5, 8>, [10, 13>} where we want to add a subrange
                //
                // A) [0, 5>                              ^
                // B) [3, 6>, [3, 10>, [7, 9>             ^
                // C) [8, 10>                                     ^
                // D) [8, 11>                                     ^
                // E) [13, 15>                                           ^
                //                                                        \__ we'll leave the while loop with iter pointing to.
                //
                // If our subrange overlaps with any existing element (it may not overlap with more than one element)
                // then we have to extend the subrange of that element and the texture must be REPLACED.
                //
                // If our subrange does NOT overlap then either it should be inserted as a new element if the texture
                // is different from the element before and after it, or if it is equal to one of those (it can not
                // be equal to both(?)) then it must be merged with that one.
                //
                if (!(iter->first.subrange() < texture_update_request->subrange()))
                {
                  same_factory_characteristic_id = true;
                  break;
                }
                if (same_texture)
                  break;
                ++iter;
              }
              if (!same_factory_characteristic_id || texture_update_request->subrange() < iter->first.subrange())
              {
                // New subrange (case A, C, and E) - if same_factory_characteristic_id: merge with iter if texture* is the same, otherwise possibly merge with
                //                                                                      the prev element, if any, if that has a texture* that is the same.
                if (same_texture)
                {
                  iter->first.extend_subrange(texture_update_request->subrange());
#if CW_DEBUG
                  auto iter2 = iter;
                  ++iter2;
                  // If a characteristic range has multiple textures then those must be in contiguous subranges.
                  ASSERT(iter2 == m_factory_characteristic_key_to_texture.end() ||
                      iter->first.id() != texture_update_request->factory_characteristic_id() ||
                      texture_update_request->subrange() < iter2->first.subrange());
#endif
                }
                else
                {
                  // New texture and non-overlapping subrange. Insert a new element.
                  m_factory_characteristic_key_to_texture.insert(iter,
                      factory_characteristic_key_to_texture_t::value_type{key, texture});
#if CW_DEBUG
                  auto iter2 = iter;
                  ++iter2;
                  while (iter2 != m_factory_characteristic_key_to_texture.end() &&
                      iter2->first.id() == texture_update_request->factory_characteristic_id())
                  {
                    // If a characteristic range has multiple textures then those must be in contiguous subranges.
                    ASSERT(iter2->second != texture);
                  }
#endif
                }
              }
              else
              {
                // Overlapping subrange (case B and D) - merge and update(?) texture.
                iter->first.extend_subrange(texture_update_request->subrange());
#if CW_DEBUG
                auto iter2 = iter;
                ++iter2;
                // If a characteristic range has multiple textures then those must be in contiguous subranges.
                ASSERT(iter2 == m_factory_characteristic_key_to_texture.end() ||
                    iter->first.id() != texture_update_request->factory_characteristic_id() ||
                    texture_update_request->subrange() < iter2->first.subrange());
#endif
                if (!same_texture)
                {
                  // Replace texture.
                  iter->second = texture;
                }
              }
            }
            // Find matching descriptors and update them.
            auto matching_descriptors = find_descriptors(key);
#if CW_DEBUG
            if (matching_descriptors.first != matching_descriptors.second)
            {
              LogicalDevice const* logical_device = m_owning_window->logical_device();
              if (!logical_device->supports_sampled_image_update_after_bind())
              {
                // In principle this is a bug in the program: it should call LogicalDevice::supports_sampled_image_update_after_bind()
                // to test if it is allowed to change textures on descriptors that are bound to a pipeline.
                DoutFatal(dc::core, "The PipelineFactory using the CombinedImageSamplerUpdater \"" << debug_name() << "\" was run before update_image_sampler[_array] was called on that CombinedImageSamplerUpdater while your vulkan device is not supporting descriptorBindingSampledImageUpdateAfterBind! In that case calls to update_image_sampler[_array] can only be done from create_textures.");
              }
              // Call set_bindings_flags(vk::DescriptorBindingFlagBits::eUpdateAfterBind) on the CombinedImageSamplerUpdater that owns this task.
              ASSERT((m_binding_flags.load(std::memory_order::relaxed) & vk::DescriptorBindingFlagBits::eUpdateAfterBind));
            }
#endif
            for (auto descriptor = matching_descriptors.first; descriptor != matching_descriptors.second; ++descriptor)
              texture->update_descriptor_array(m_owning_window, descriptor->second.descriptor_set(), descriptor->second.binding(), /*array_elements*/ {0, 1});  //FIXME: use the real array element values.
          }
        });
      if (producer_not_finished())
        break;
      set_state(CombinedImageSamplerUpdater_done);
      [[fallthrough]];
    case CombinedImageSamplerUpdater_done:
      finish();
      break;
  }
}

#ifdef CWDEBUG
namespace detail {
using utils::has_print_on::operator<<;

void CombinedImageSamplerShaderResourceMember::print_on(std::ostream& os) const
{
  os << '{';
  os << "m_member:" << m_member;
  os << '}';
}

} // namespace detail

void CombinedImageSamplerUpdater::print_on(std::ostream& os) const
{
  os << '{';
  os << "(ShaderResourceBase)";
  ShaderResourceBase::print_on(os);
  os << ", m_member:" << vk_utils::print_pointer(m_member);
  os << '}';
}

#endif // CWDEBUG

} // namespace vulkan::descriptor
