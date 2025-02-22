/* ConditionsStore.cpp
Copyright (c) 2020-2022 by Peter van der Meer

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "ConditionsStore.h"

#include "DataNode.h"
#include "DataWriter.h"

#include <utility>

using namespace std;



// Default constructor
ConditionsStore::DerivedProvider::DerivedProvider(const string &name, bool isPrefixProvider)
	: name(name), isPrefixProvider(isPrefixProvider)
{
}



void ConditionsStore::DerivedProvider::SetGetFunction(function<int64_t(const string &)> newGetFun)
{
	getFunction = std::move(newGetFun);
}



void ConditionsStore::DerivedProvider::SetHasFunction(function<bool(const string &)> newHasFun)
{
	hasFunction = std::move(newHasFun);
}



void ConditionsStore::DerivedProvider::SetSetFunction(function<bool(const string &, int64_t)> newSetFun)
{
	setFunction = std::move(newSetFun);
}



void ConditionsStore::DerivedProvider::SetEraseFunction(function<bool(const string &)> newEraseFun)
{
	eraseFunction = std::move(newEraseFun);
}



ConditionsStore::ConditionEntry::operator int64_t() const
{
	if(!provider)
		return value;

	const string &key = fullKey.empty() ? provider->name : fullKey;
	return provider->getFunction(key);
}



ConditionsStore::ConditionEntry &ConditionsStore::ConditionEntry::operator=(int64_t val)
{
	if(!provider)
		value = val;
	else
	{
		const string &key = fullKey.empty() ? provider->name : fullKey;
		provider->setFunction(key, val);
	}
	return *this;
}



ConditionsStore::ConditionEntry &ConditionsStore::ConditionEntry::operator++()
{
	if(!provider)
		++value;
	else
	{
		const string &key = fullKey.empty() ? provider->name : fullKey;
		provider->setFunction(key, provider->getFunction(key) + 1);
	}
	return *this;
}



ConditionsStore::ConditionEntry &ConditionsStore::ConditionEntry::operator--()
{
	if(!provider)
		--value;
	else
	{
		const string &key = fullKey.empty() ? provider->name : fullKey;
		provider->setFunction(key, provider->getFunction(key) - 1);
	}
	return *this;
}



ConditionsStore::ConditionEntry &ConditionsStore::ConditionEntry::operator+=(int64_t val)
{
	if(!provider)
		value += val;
	else
	{
		const string &key = fullKey.empty() ? provider->name : fullKey;
		provider->setFunction(key, provider->getFunction(key) + val);
	}
	return *this;
}



ConditionsStore::ConditionEntry &ConditionsStore::ConditionEntry::operator-=(int64_t val)
{
	if(!provider)
		value -= val;
	else
	{
		const string &key = fullKey.empty() ? provider->name : fullKey;
		provider->setFunction(key, provider->getFunction(key) - val);
	}
	return *this;
}



ConditionsStore::PrimariesIterator::PrimariesIterator(CondMapItType it, CondMapItType endIt)
	: condMapIt(it), condMapEnd(endIt)
{
	MoveToValueCondition();
};



pair<string, int64_t> ConditionsStore::PrimariesIterator::operator*() const
{
	return itVal;
}



const pair<string, int64_t> *ConditionsStore::PrimariesIterator::operator->()
{
	return &itVal;
}



ConditionsStore::PrimariesIterator &ConditionsStore::PrimariesIterator::operator++()
{
	condMapIt++;
	MoveToValueCondition();
	return *this;
};



ConditionsStore::PrimariesIterator ConditionsStore::PrimariesIterator::operator++(int)
{
	PrimariesIterator tmp = *this;
	condMapIt++;
	MoveToValueCondition();
	return tmp;
};



// Equation operators, we can just compare the upstream iterators.
bool ConditionsStore::PrimariesIterator::operator==(const ConditionsStore::PrimariesIterator &rhs) const
{
	return condMapIt == rhs.condMapIt;
}



bool ConditionsStore::PrimariesIterator::operator!=(const ConditionsStore::PrimariesIterator &rhs) const
{
	return condMapIt != rhs.condMapIt;
}



// Helper function to ensure that the primary-conditions iterator points
// to a primary (value) condition or to the end-iterator value.
void ConditionsStore::PrimariesIterator::MoveToValueCondition()
{
	while((condMapIt != condMapEnd) && (condMapIt->second).provider)
		condMapIt++;

	if(condMapIt != condMapEnd)
		itVal = make_pair(condMapIt->first, (condMapIt->second).value);
}



// Constructor with loading primary conditions from datanode.
ConditionsStore::ConditionsStore(const DataNode &node)
{
	Load(node);
}



// Constructor where a number of initial manually-set values are set.
ConditionsStore::ConditionsStore(initializer_list<pair<string, int64_t>> initialConditions)
{
	for(const auto &it : initialConditions)
		Set(it.first, it.second);
}



// Constructor where a number of initial manually-set values are set.
ConditionsStore::ConditionsStore(const map<string, int64_t> &initialConditions)
{
	for(const auto &it : initialConditions)
		Set(it.first, it.second);
}



void ConditionsStore::Load(const DataNode &node)
{
	for(const DataNode &child : node)
		Set(child.Token(0), (child.Size() >= 2) ? child.Value(1) : 1);
}



void ConditionsStore::Save(DataWriter &out) const
{
	if(PrimariesBegin() != PrimariesEnd())
	{
		out.Write("conditions");
		out.BeginChild();
		{
			for(auto it = PrimariesBegin(); it != PrimariesEnd(); ++it)
			{
				// If the condition's value is 1, don't bother writing the 1.
				if(it->second == 1)
					out.Write(it->first);
				else if(it->second)
					out.Write(it->first, it->second);
			}
		}
		out.EndChild();
	}
}



// Get a condition from the Conditions-Store. Retrieves both conditions
// that were directly set (primary conditions) as well as conditions
// derived from other data-structures (derived conditions).
int64_t ConditionsStore::Get(const string &name) const
{
	const ConditionEntry *ce = GetEntry(name);
	if(!ce)
		return 0;

	if(!ce->provider)
		return ce->value;

	return ce->provider->getFunction(name);
}



bool ConditionsStore::Has(const string &name) const
{
	const ConditionEntry *ce = GetEntry(name);
	if(!ce)
		return false;

	if(!ce->provider)
		return true;

	return ce->provider->hasFunction(name);
}



// Returns a pair where the boolean indicates if the game has this condition set,
// and an int64_t which contains the value if the condition was set.
pair<bool, int64_t> ConditionsStore::HasGet(const string &name) const
{
	const ConditionEntry *ce = GetEntry(name);
	if(!ce)
		return make_pair(false, 0);

	if(!ce->provider)
		return make_pair(true, ce->value);

	bool has = ce->provider->hasFunction(name);
	int64_t val = 0;
	if(has)
		val = ce->provider->getFunction(name);

	return make_pair(has, val);
}



// Add a value to a condition. Returns true on success, false on failure.
bool ConditionsStore::Add(const string &name, int64_t value)
{
	// This code performers 2 lookups of the condition, once for get and
	// once for set. This might be optimized to a single lookup in a
	// later version of the code.
	return Set(name, Get(name) + value);
}



// Set a value for a condition, either for the local value, or by performing
// a set on the provider.
bool ConditionsStore::Set(const string &name, int64_t value)
{
	ConditionEntry *ce = GetEntry(name);
	if(!ce)
	{
		(storage[name]).value = value;
		return true;
	}
	if(!ce->provider)
	{
		ce->value = value;
		return true;
	}
	return ce->provider->setFunction(name, value);
}



// Erase a condition completely, either the local value or by performing
// an erase on the provider.
bool ConditionsStore::Erase(const string &name)
{
	ConditionEntry *ce = GetEntry(name);
	if(!ce)
		return true;

	if(!(ce->provider))
	{
		storage.erase(name);
		return true;
	}
	return ce->provider->eraseFunction(name);
}



ConditionsStore::ConditionEntry &ConditionsStore::operator[](const string &name)
{
	// Search for an exact match and return it if it exists.
	auto it = storage.find(name);
	if(it != storage.end())
		return it->second;

	// Check for a prefix provider.
	ConditionEntry *ceprov = GetEntry(name);
	// If no prefix provider is found, then just create a new value entry.
	if(ceprov == nullptr)
		return storage[name];

	// Found a matching prefixed entry provider, but no exact match for the entry itself,
	// let's create the exact match based on the prefix provider.
	ConditionEntry &ce = storage[name];
	ce.provider = ceprov->provider;
	ce.fullKey = name;
	return ce;
}



ConditionsStore::PrimariesIterator ConditionsStore::PrimariesBegin() const
{
	return PrimariesIterator(storage.begin(), storage.end());
}



ConditionsStore::PrimariesIterator ConditionsStore::PrimariesEnd() const
{
	return PrimariesIterator(storage.end(), storage.end());
}



ConditionsStore::PrimariesIterator ConditionsStore::PrimariesLowerBound(const string &key) const
{
	return PrimariesIterator(storage.lower_bound(key), storage.end());
}



// Build a provider for a given prefix.
ConditionsStore::DerivedProvider &ConditionsStore::GetProviderPrefixed(const string &prefix)
{
	auto it = providers.emplace(std::piecewise_construct,
		std::forward_as_tuple(prefix),
		std::forward_as_tuple(prefix, true));
	storage[prefix].provider = &(it.first->second);
	return it.first->second;
}



// Build a provider for the condition identified by the given name.
ConditionsStore::DerivedProvider &ConditionsStore::GetProviderNamed(const string &name)
{
	auto it = providers.emplace(std::piecewise_construct,
		std::forward_as_tuple(name),
		std::forward_as_tuple(name, false));
	storage[name].provider = &(it.first->second);
	return it.first->second;
}



// Helper to completely remove all data and linked condition-providers from the store.
void ConditionsStore::Clear()
{
	storage.clear();
	providers.clear();
}



ConditionsStore::ConditionEntry *ConditionsStore::GetEntry(const string &name)
{
	// Avoid code-duplication between const and non-const function.
	return const_cast<ConditionsStore::ConditionEntry *>(const_cast<const ConditionsStore *>(this)->GetEntry(name));
}



const ConditionsStore::ConditionEntry *ConditionsStore::GetEntry(const string &name) const
{
	if(storage.empty())
		return nullptr;

	// Perform a single search for values, named providers, and prefixed providers.
	auto it = storage.upper_bound(name);
	if(it == storage.begin())
		return nullptr;

	--it;
	// The entry is matching if we have an exact string match.
	if(!name.compare(it->first))
		return &(it->second);

	// The entry is also matching when we have a prefix entry and the prefix part in the provider matches.
	DerivedProvider *provider = it->second.provider;
	if(provider && provider->isPrefixProvider && !name.compare(0, provider->name.length(), provider->name))
		return &(it->second);

	// And otherwise we don't have a match.
	return nullptr;
}
