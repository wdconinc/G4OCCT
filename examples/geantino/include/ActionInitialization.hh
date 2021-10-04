#ifndef _ACTIONINITIALIZATION_HH_
#define _ACTIONINITIALIZATION_HH_

#include "G4VUserActionInitialization.hh"

#include "PrimaryGeneratorAction.hh"

class ActionInitialization: public G4VUserActionInitialization
{
  public:
    ActionInitialization() = default;
    virtual ~ActionInitialization() = default;

    virtual void BuildForMaster() const { };
    virtual void Build() const {
      SetUserAction(new PrimaryGeneratorAction);
    };
};

#endif
